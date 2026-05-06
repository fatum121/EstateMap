// =============================================================================
// RCN Downloader - EstateMap edition (z synchronizacją przyrostową)
//
// Pierwsze uruchomienie: pobiera wszystkie lokale mieszkalne.
// Kolejne uruchomienia: zlicza linie w CSV i startuje od STARTINDEX = liczba_linii.
//   Serwer zawsze zwraca rekordy w tej samej kolejności → pomijamy już pobrane.
//
// Format CSV (6 kolumn):
//   woj, powiat, miasto, rok_miesiac, cena, pow
//
// Kompilacja:
//   g++ -std=c++17 -o rcn main.cpp
//       $(gdal-config --libs) $(gdal-config --cflags) -lcurl -lpthread
// =============================================================================

#include "gdal_priv.h"
#include "ogrsf_frmts.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstring>
#include <dirent.h>
#include <curl/curl.h>

// ---------------------------------------------------------------------------
// Konfiguracja
// ---------------------------------------------------------------------------
static const std::string WFS_URL       = "https://mapy.geoportal.gov.pl/wss/service/rcn";
static const std::string OUTPUT_CSV    = std::string(PROJECT_ROOT) + "/data/transakcje_rcn.csv";
static const std::string PROGRESS_FILE = std::string(PROJECT_ROOT) + "/data/rcn_progress.txt";
static const int         PAGE_SIZE     = 5000;
static const int         NUM_THREADS   = 8; //watki
static const int         MAX_RETRIES   = 5;
static const int         RETRY_DELAY   = 10;

static const double MIN_POW     =    5.0;
static const double MAX_POW     = 1000.0;
static const double MIN_CENA    = 1000.0;
static const double MIN_CENA_M2 = 1000.0;
static const double MAX_CENA_M2 = 50000.0;

static const std::string PROPERTY_NAMES =
    "ms:teryt,"
    "ms:dok_data,"
    "ms:tran_cena_brutto,"
    "ms:lok_pow_uzyt,"
    "ms:lok_adres";

static const int IDX_TERYT     = 0;
static const int IDX_DOK_DATA  = 1;
static const int IDX_CENA      = 2;
static const int IDX_POW       = 3;
static const int IDX_ADRES     = 4;
static const std::vector<std::string> FIELDS = {
    "teryt", "dok_data", "tran_cena_brutto",
    "lok_pow_uzyt", "lok_adres"
};

static const std::string CQL_BASE =
    "lok_funkcja%3D%27mieszkalna%27"
    "%20AND%20lok_pow_uzyt%20IS%20NOT%20NULL"
    "%20AND%20lok_pow_uzyt%20%3E%200"
    "%20AND%20tran_cena_brutto%20IS%20NOT%20NULL"
    "%20AND%20tran_cena_brutto%20%3E%200";

static const std::string CSV_HEADER = "woj,powiat,miasto,rok_miesiac,cena,pow";

// ---------------------------------------------------------------------------
// Globalne — synchronizacja wątków
// ---------------------------------------------------------------------------
struct PageResult {
    int         startIndex;
    int         recordCount;
    bool        isLast;
    std::vector<std::vector<std::string>> rows;
};

std::mutex            csvMutex;
std::mutex            logMutex;
std::atomic<int>      nextPageIndex(0);
std::atomic<int>      totalWritten(0);
std::atomic<int>      totalOdrzuconych(0);
std::atomic<bool>     fetchingDone(false);
std::map<int, PageResult> pageBuffer;
std::mutex            bufferMutex;
int                   nextToWrite = 0;

// ---------------------------------------------------------------------------
// Czyszczenie plików tymczasowych
// ---------------------------------------------------------------------------
void czyscWszystkiePlikiTymczasowe() {
    DIR* dir = opendir(".");
    if (!dir) return;

    int usunieto = 0;
    struct dirent* entry;
    const std::string prefix = "_rcn_tmp_";
    const std::string suffix = ".gml";

    while ((entry = readdir(dir)) != NULL) {
        std::string nazwa(entry->d_name);
        if (nazwa.size() > prefix.size() + suffix.size() &&
            nazwa.substr(0, prefix.size()) == prefix &&
            nazwa.substr(nazwa.size() - suffix.size()) == suffix)
        {
            if (std::remove(nazwa.c_str()) == 0) usunieto++;
        }
    }
    closedir(dir);

    if (usunieto > 0)
        std::cout << "Usunieto " << usunieto << " plikow tymczasowych." << std::endl;
}

void handleSignal(int) {
    std::cout << "\n\nPrzerwano! Czyszcze pliki tymczasowe..." << std::endl;
    czyscWszystkiePlikiTymczasowe();
    std::cout << "Postep zachowany - uruchom ponownie aby wznowic." << std::endl;
    std::exit(1);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* out) {
    out->append((char*)contents, size * nmemb);
    return size * nmemb;
}

void logMsg(const std::string& msg) {
    std::lock_guard<std::mutex> lock(logMutex);
    std::cout << msg << std::endl;
}

void czekaj(int s) {
    std::this_thread::sleep_for(std::chrono::seconds(s));
}

// ---------------------------------------------------------------------------
// Parsowanie i walidacja
// ---------------------------------------------------------------------------
// Uzupełnij TERYT zerami wiodącymi do 4 znaków
// Serwer WFS zwraca np. "219" zamiast "0219" dla powiatu świdnickiego
std::string padTeryt(const std::string& t) {
    if (t.size() >= 4) return t;
    return std::string(4 - t.size(), '0') + t;
}

std::string kodWoj(const std::string& t) {
    return padTeryt(t).substr(0, 2);
}

std::string kodPowiat(const std::string& t) {
    return padTeryt(t).substr(0, 4);
}

std::string rokMiesiac(const std::string& d) {
    return d.size() >= 7 ? d.substr(0, 7) : "";
}

std::string parsujMiasto(const std::string& adres) {
    const std::string prefix = "MSC:";
    size_t s = adres.find(prefix);
    if (s == std::string::npos) return "";
    s += prefix.size();
    size_t e = adres.find(';', s);
    return adres.substr(s, e == std::string::npos ? std::string::npos : e - s);
}

bool toDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    try {
        size_t idx;
        out = std::stod(s, &idx);
        return idx > 0;
    } catch (...) { return false; }
}

bool rekordPoprawny(const std::string& cenaSt, const std::string& powSt) {
    double cena, pow;
    if (!toDouble(cenaSt, cena)) return false;
    if (!toDouble(powSt,  pow))  return false;
    if (pow  < MIN_POW  || pow  > MAX_POW)  return false;
    if (cena < MIN_CENA)                    return false;
    double cena_m2 = cena / pow;
    if (cena_m2 < MIN_CENA_M2 || cena_m2 > MAX_CENA_M2) return false;
    return true;
}

std::string escapeCSV(const std::string& v) {
    if (v.find(',') == std::string::npos &&
        v.find('"') == std::string::npos &&
        v.find('\n') == std::string::npos) return v;
    std::string o = "\"";
    for (char c : v) { if (c == '"') o += "\"\""; else o += c; }
    return o + "\"";
}


// ---------------------------------------------------------------------------
// Postęp paginacji
// ---------------------------------------------------------------------------
void zapiszPostep(int idx, int razem) {
    std::lock_guard<std::mutex> lock(csvMutex);
    std::ofstream f(PROGRESS_FILE);
    f << idx << "\n" << razem << "\n";
}

bool wczytajPostep(int& idx, int& razem) {
    std::ifstream f(PROGRESS_FILE);
    if (!f.is_open()) return false;
    f >> idx >> razem;
    return true;
}

// ---------------------------------------------------------------------------
// Pobierz jedną stronę WFS → PageResult
// ---------------------------------------------------------------------------
PageResult pobierzStrone(const std::string& typeName,
                         int startIndex)
{
    PageResult result{ startIndex, -1, false, {} };

    std::ostringstream url;
    url << WFS_URL
        << "?SERVICE=WFS&VERSION=2.0.0&REQUEST=GetFeature"
        << "&TYPENAMES="    << typeName
        << "&PROPERTYNAME=" << PROPERTY_NAMES
        << "&CQL_FILTER="   << CQL_BASE
        << "&COUNT="        << PAGE_SIZE
        << "&STARTINDEX="   << startIndex
        << "&OUTPUTFORMAT=application%2Fgml%2Bxml%3B+version%3D3.2";

    for (int proba = 1; proba <= MAX_RETRIES; proba++) {
        CURL* curl = curl_easy_init();
        if (!curl) break;

        std::string bufor;
        curl_easy_setopt(curl, CURLOPT_URL,            url.str().c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &bufor);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT,        180L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE,  1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE,   60L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL,  15L);

        struct curl_slist* h = NULL;
        h = curl_slist_append(h, "Accept: application/xml, text/xml");
        h = curl_slist_append(h, "User-Agent: Mozilla/5.0 (compatible; RCN-WFS-Client/2.0)");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);

        CURLcode res = curl_easy_perform(curl);
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        curl_slist_free_all(h);
        curl_easy_cleanup(curl);

        bool ok = res == CURLE_OK &&
                  httpCode >= 200 && httpCode < 300 &&
                  bufor.find("ExceptionReport") == std::string::npos &&
                  bufor.find("ServiceException") == std::string::npos;

        if (!ok) {
            if (proba < MAX_RETRIES) {
                logMsg("  [idx=" + std::to_string(startIndex) + "] proba " +
                       std::to_string(proba) + "/" + std::to_string(MAX_RETRIES) +
                       ", czekam " + std::to_string(RETRY_DELAY * proba) + "s...");
                czekaj(RETRY_DELAY * proba);
            }
            continue;
        }

        std::string tmp = "_rcn_tmp_" + std::to_string(startIndex) + ".gml";
        { std::ofstream f(tmp); f << bufor; }

        GDALDataset* ds = (GDALDataset*)GDALOpenEx(
            tmp.c_str(), GDAL_OF_VECTOR, NULL, NULL, NULL);
        if (!ds) { std::remove(tmp.c_str()); continue; }

        OGRLayer* layer = ds->GetLayer(0);
        if (!layer) {
            GDALClose(ds); std::remove(tmp.c_str());
            result.recordCount = 0; result.isLast = true;
            return result;
        }

        OGRFeatureDefn* fdef = layer->GetLayerDefn();
        std::vector<int> fieldIdx;
        for (const auto& name : FIELDS)
            fieldIdx.push_back(fdef->GetFieldIndex(name.c_str()));

        OGRFeature* feat;
        layer->ResetReading();
        while ((feat = layer->GetNextFeature()) != NULL) {
            std::vector<std::string> row;
            for (int i : fieldIdx)
                row.push_back(i >= 0 ? feat->GetFieldAsString(i) : "");
            result.rows.push_back(std::move(row));
            OGRFeature::DestroyFeature(feat);
        }

        GDALClose(ds);
        std::remove(tmp.c_str());

        result.recordCount = (int)result.rows.size();
        result.isLast      = result.recordCount < PAGE_SIZE;
        return result;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Zapis bufora do CSV w kolejności rosnącej
// ---------------------------------------------------------------------------
void zapiszBufforDoCSV(std::ofstream& csv, bool& pisNaglowek) {
    std::lock_guard<std::mutex> bl(bufferMutex);
    std::lock_guard<std::mutex> cl(csvMutex);

    while (true) {
        auto it = pageBuffer.find(nextToWrite);
        if (it == pageBuffer.end()) break;

        if (pisNaglowek) {
            csv << CSV_HEADER << "\n";
            pisNaglowek = false;
        }

        int odrzucone = 0;
        for (const auto& row : it->second.rows) {
            if (!rekordPoprawny(row[IDX_CENA], row[IDX_POW])) {
                odrzucone++;
                continue;
            }

            std::string woj      = kodWoj(row[IDX_TERYT]);
            std::string powiat   = kodPowiat(row[IDX_TERYT]);
            std::string miasto   = parsujMiasto(row[IDX_ADRES]);
            std::string rok_mies = rokMiesiac(row[IDX_DOK_DATA]);

            if (woj.empty() || miasto.empty() || rok_mies.empty()) {
                odrzucone++;
                continue;
            }

            csv << escapeCSV(woj)           << ","
                << escapeCSV(powiat)        << ","
                << escapeCSV(miasto)        << ","
                << escapeCSV(rok_mies)      << ","
                << escapeCSV(row[IDX_CENA]) << ","
                << escapeCSV(row[IDX_POW])  << "\n";
        }

        totalOdrzuconych += odrzucone;

        totalWritten += (int)it->second.rows.size() - odrzucone;
        nextToWrite  += PAGE_SIZE;
        pageBuffer.erase(it);
    }
}

// ---------------------------------------------------------------------------
// Wątek roboczy
// ---------------------------------------------------------------------------
void workerThread(const std::string& typeName,
                  int baseStart, std::ofstream& csv, bool& pisNaglowek,
                  std::atomic<bool>& errorFlag)
{
    while (!fetchingDone.load() && !errorFlag.load()) {
        int pageNum    = nextPageIndex.fetch_add(1);
        int startIndex = baseStart + pageNum * PAGE_SIZE;

        logMsg("  Strona " + std::to_string(pageNum + 1) +
               " (rekordy " + std::to_string(startIndex + 1) +
               "-" + std::to_string(startIndex + PAGE_SIZE) + ")...");

        PageResult result = pobierzStrone(typeName, startIndex);

        if (result.recordCount < 0) {
            logMsg("BLAD: nie udalo sie pobrac od " + std::to_string(startIndex));
            errorFlag = true;
            break;
        }

        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            if (result.isLast) fetchingDone = true;
            pageBuffer[startIndex] = std::move(result);
        }

        zapiszBufforDoCSV(csv, pisNaglowek);

        logMsg("  -> zapisano: " + std::to_string(totalWritten.load()) +
               " | odrzucono: " + std::to_string(totalOdrzuconych.load()));
    }
}

// ---------------------------------------------------------------------------
// GetCapabilities
// ---------------------------------------------------------------------------
std::string pobierzNazweWarstwy() {
    CURL* curl = curl_easy_init();
    if (!curl) return "ms:lokale";

    std::string bufor;
    std::string url = WFS_URL + "?SERVICE=WFS&VERSION=2.0.0&REQUEST=GetCapabilities";
    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &bufor);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    size_t pos = bufor.find("FeatureTypeList");
    if (pos == std::string::npos) pos = 0;
    size_t np = bufor.find("<n>", pos);
    if (np != std::string::npos) {
        size_t ep = bufor.find("</n>", np);
        if (ep != std::string::npos) {
            std::string name = bufor.substr(np + 6, ep - np - 6);
            std::cout << "Warstwa WFS: " << name << std::endl;
            return name;
        }
    }
    std::cout << "Uzywam domyslnej: ms:budynki" << std::endl;
    return "ms:lokale";
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    std::signal(SIGINT,  handleSignal);
    std::signal(SIGTERM, handleSignal);

    // Upewnij się że katalog data/ istnieje
    std::filesystem::create_directories(std::string(PROJECT_ROOT) + "/data");

    czyscWszystkiePlikiTymczasowe();

    GDALAllRegister();
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Wyznacz startIndex z PROGRESS_FILE który przechowuje ile rekordów
    // serwer zwrócił łącznie (zapisane + odrzucone przez filtr C++).
    // Dzięki temu nie pobieramy drugi raz rekordów już odrzuconych.
    int baseStart = 0, poprzednio = 0;
    bool wznawianie = wczytajPostep(baseStart, poprzednio);

    std::cout << "========================================================\n"
              << " RCN Downloader - EstateMap\n";
    if (baseStart > 0)
        std::cout << " TRYB: synchronizacja przyrostowa\n"
                  << " Startuje od rekordu " << baseStart + 1
                  << " (serwer zwrocil " << baseStart << " rekordow poprzednio)\n";
    else
        std::cout << " TRYB: pelne pobieranie\n";

    std::cout << " Watki: " << NUM_THREADS << " | Strona: " << PAGE_SIZE << "\n"
              << " Filtr C++: pow " << MIN_POW << "-" << MAX_POW << "m2"
              << ", cena/m2 " << MIN_CENA_M2 << "-" << MAX_CENA_M2 << "zl/m2\n"
              << " Kolumny: " << CSV_HEADER << "\n";
    std::cout << "========================================================\n\n";

    std::string typeName = pobierzNazweWarstwy();

    bool appendMode  = (baseStart > 0);
    std::ofstream csv(OUTPUT_CSV, appendMode ? std::ios::app : std::ios::out);
    if (!csv.is_open()) {
        std::cerr << "Nie mozna otworzyc " << OUTPUT_CSV << std::endl;
        curl_global_cleanup();
        return 1;
    }

    bool pisNaglowek = !appendMode;
    totalWritten     = poprzednio;
    nextToWrite      = baseStart;
    nextPageIndex    = 0;

    std::cout << "Rozpoczynam pobieranie (" << NUM_THREADS << " watkow)...\n\n";

    std::atomic<bool> errorFlag(false);
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; i++)
        threads.emplace_back(workerThread,
            std::ref(typeName), baseStart,
            std::ref(csv), std::ref(pisNaglowek),
            std::ref(errorFlag));

    for (auto& t : threads) t.join();

    zapiszBufforDoCSV(csv, pisNaglowek);
    csv.close();
    curl_global_cleanup();

    if (errorFlag.load()) {
        std::cerr << "\nPrzerwano z powodu bledu.\n"
                  << "Postep zapisany - uruchom ponownie aby wznowic.\n";
        zapiszPostep(nextToWrite, totalWritten.load());
        return 1;
    }

    // Zapisz ile rekordów serwer zwrócił łącznie — używane przy następnym starcie
    zapiszPostep(nextToWrite, totalWritten.load());

    std::cout << "\n========================================================\n"
              << " Gotowe!\n"
              << " Zapisano lokali : " << totalWritten.load()     << "\n"
              << " Odrzucono       : " << totalOdrzuconych.load()
              << " (puste/bledne pow lub cena)\n"
              << " Plik            : " << OUTPUT_CSV              << "\n"
              << "========================================================\n";
    return 0;
}