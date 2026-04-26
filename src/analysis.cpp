// =============================================================================
// EstateMap - analysis.cpp
// Wczytuje transakcje_rcn.csv, oblicza statystyki per województwo i miasto,
// zapisuje output/data.json gotowy do wczytania przez Leaflet.
//
// Kompilacja (osobny target w CMakeLists):
//   g++ -std=c++17 -O2 -o analysis analysis.cpp
//
// Użycie:
//   ./analysis [ścieżka_do_csv] [ścieżka_do_output]
//   ./analysis                                          <- domyślnie ../data/transakcje_rcn.csv
// =============================================================================

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iomanip>

// ---------------------------------------------------------------------------
// Struktury
// ---------------------------------------------------------------------------

struct PropertyRecord {
    std::string woj;        // "14"
    std::string powiat;     // "1465"
    std::string miasto;     // "Warszawa"
    std::string rok_mies;   // "2024-06"
    double      cena;       // 670000.0
    double      pow;        // 64.5
    double      cena_m2;    // wyliczone: cena / pow
    int         rok;        // 2024
};

struct WojStats {
    std::string kod;            // "14"
    double      avg_cena_m2;    // średnia cena/m²
    double      median_cena_m2; // mediana ceny/m²
    double      trend;          // % zmiana rok do roku
    int         liczba_transakcji;
    std::string nazwa;          // "mazowieckie"
};

struct MiastoStats {
    std::string miasto;
    std::string woj;
    double      avg_cena_m2;
    int         liczba_transakcji;
};

// ---------------------------------------------------------------------------
// Tabela kodów województw
// ---------------------------------------------------------------------------
static const std::map<std::string, std::string> NAZWY_WOJ = {
    {"02", "dolnoslaskie"},
    {"04", "kujawsko-pomorskie"},
    {"06", "lubelskie"},
    {"08", "lubuskie"},
    {"10", "lodzkie"},
    {"12", "malopolskie"},
    {"14", "mazowieckie"},
    {"16", "opolskie"},
    {"18", "podkarpackie"},
    {"20", "podlaskie"},
    {"22", "pomorskie"},
    {"24", "slaskie"},
    {"26", "swietokrzyskie"},
    {"28", "warminsko-mazurskie"},
    {"30", "wielkopolskie"},
    {"32", "zachodniopomorskie"}
};

// ---------------------------------------------------------------------------
// Parser CSV
// ---------------------------------------------------------------------------

// Parsuje jeden pole CSV (obsługa cudzysłowów)
std::string parseField(const std::string& s) {
    if (s.empty()) return "";
    if (s[0] == '"') {
        std::string out;
        for (size_t i = 1; i < s.size() - 1; i++) {
            if (s[i] == '"' && i + 1 < s.size() && s[i+1] == '"') {
                out += '"'; i++;
            } else {
                out += s[i];
            }
        }
        return out;
    }
    return s;
}

// Parsuje linię CSV na pola
std::vector<std::string> parseLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (c == '"') {
            if (inQuotes && i + 1 < line.size() && line[i+1] == '"') {
                field += '"'; i++;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == ',' && !inQuotes) {
            fields.push_back(field);
            field.clear();
        } else {
            field += c;
        }
    }
    fields.push_back(field);
    return fields;
}

// Bezpieczna konwersja na double
bool toDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    try {
        size_t idx;
        out = std::stod(s, &idx);
        return idx > 0;
    } catch (...) { return false; }
}

// Wczytuje CSV → vector<PropertyRecord>
std::vector<PropertyRecord> loadCSV(const std::string& path) {
    std::vector<PropertyRecord> records;
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "Nie mozna otworzyc: " << path << std::endl;
        return records;
    }

    std::string line;
    int linia = 0;
    int odrzucone = 0;

    while (std::getline(f, line)) {
        linia++;

        // Pomiń nagłówek i metadaną #SYNC
        if (linia == 1) continue;
        if (!line.empty() && line[0] == '#') continue;
        if (line.empty()) continue;

        auto fields = parseLine(line);
        if (fields.size() < 6) { odrzucone++; continue; }

        PropertyRecord r;
        r.woj      = fields[0];
        r.powiat   = fields[1];
        r.miasto   = fields[2];
        r.rok_mies = fields[3];

        if (!toDouble(fields[4], r.cena)) { odrzucone++; continue; }
        if (!toDouble(fields[5], r.pow))  { odrzucone++; continue; }
        if (r.pow <= 0) { odrzucone++; continue; }

        r.cena_m2 = r.cena / r.pow;

        // Wyciągnij rok z "2024-06"
        r.rok = 0;
        if (r.rok_mies.size() >= 4) {
            try { r.rok = std::stoi(r.rok_mies.substr(0, 4)); }
            catch (...) {}
        }

        records.push_back(std::move(r));
    }

    std::cout << "Wczytano: " << records.size() << " rekordow"
              << " (odrzucono: " << odrzucone << ")" << std::endl;
    return records;
}

// ---------------------------------------------------------------------------
// Analiza
// ---------------------------------------------------------------------------

double median(std::vector<double>& v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return n % 2 == 0 ? (v[n/2-1] + v[n/2]) / 2.0 : v[n/2];
}

// Oblicz trend rok-do-roku: ((avg_ostatni_rok - avg_poprzedni_rok) / avg_poprzedni_rok) * 100
double obliczTrend(const std::vector<PropertyRecord>& rekordy) {
    if (rekordy.empty()) return 0.0;

    // Znajdź najnowszy rok w danych
    int maxRok = 0;
    for (const auto& r : rekordy)
        if (r.rok > maxRok) maxRok = r.rok;

    if (maxRok < 2) return 0.0;
    int prevRok = maxRok - 1;

    std::vector<double> biezacy, poprzedni;
    for (const auto& r : rekordy) {
        if (r.rok == maxRok)  biezacy.push_back(r.cena_m2);
        if (r.rok == prevRok) poprzedni.push_back(r.cena_m2);
    }

    if (biezacy.empty() || poprzedni.empty()) return 0.0;

    double avgB = std::accumulate(biezacy.begin(),    biezacy.end(),    0.0) / biezacy.size();
    double avgP = std::accumulate(poprzedni.begin(),  poprzedni.end(),  0.0) / poprzedni.size();

    if (avgP <= 0) return 0.0;
    return ((avgB - avgP) / avgP) * 100.0;
}

// Statystyki per województwo
std::vector<WojStats> analizujWoj(const std::vector<PropertyRecord>& records) {
    // Grupuj po województwie
    std::map<std::string, std::vector<PropertyRecord>> grupy;
    for (const auto& r : records)
        grupy[r.woj].push_back(r);

    std::vector<WojStats> wyniki;
    for (auto& [kod, rekordy] : grupy) {
        if (rekordy.empty()) continue;

        WojStats s;
        s.kod  = kod;
        s.nazwa = NAZWY_WOJ.count(kod) ? NAZWY_WOJ.at(kod) : kod;
        s.liczba_transakcji = (int)rekordy.size();

        // Średnia
        double suma = 0;
        std::vector<double> wartosci;
        for (const auto& r : rekordy) {
            suma += r.cena_m2;
            wartosci.push_back(r.cena_m2);
        }
        s.avg_cena_m2    = suma / rekordy.size();
        s.median_cena_m2 = median(wartosci);
        s.trend          = obliczTrend(rekordy);

        wyniki.push_back(s);
    }

    return wyniki;
}

// Top N miast per województwo
std::map<std::string, std::vector<MiastoStats>> analizujMiasta(
    const std::vector<PropertyRecord>& records, int topN = 10)
{
    // Grupuj po woj + miasto
    std::map<std::string, std::map<std::string, std::vector<double>>> grupy;
    for (const auto& r : records)
        grupy[r.woj][r.miasto].push_back(r.cena_m2);

    std::map<std::string, std::vector<MiastoStats>> wyniki;
    for (auto& [woj, miasta] : grupy) {
        std::vector<MiastoStats> stats;
        for (auto& [miasto, ceny] : miasta) {
            if (ceny.empty()) continue;
            MiastoStats ms;
            ms.miasto  = miasto;
            ms.woj     = woj;
            ms.liczba_transakcji = (int)ceny.size();
            ms.avg_cena_m2 = std::accumulate(ceny.begin(), ceny.end(), 0.0) / ceny.size();
            stats.push_back(ms);
        }
        // Sortuj malejąco po liczbie transakcji, ogranicz do topN
        std::sort(stats.begin(), stats.end(),
            [](const MiastoStats& a, const MiastoStats& b) {
                return a.liczba_transakcji > b.liczba_transakcji;
            });
        if ((int)stats.size() > topN) stats.resize(topN);
        wyniki[woj] = stats;
    }
    return wyniki;
}

// ---------------------------------------------------------------------------
// Eksport JSON
// ---------------------------------------------------------------------------

// Escape stringa dla JSON
std::string jsonStr(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return "\"" + out + "\"";
}

std::string fmt(double v, int prec = 2) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(prec) << v;
    return ss.str();
}

void exportJSON(const std::vector<WojStats>& woj,
                const std::map<std::string, std::vector<MiastoStats>>& miasta,
                const std::string& path)
{
    // Upewnij się że katalog output istnieje
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "Nie mozna zapisac: " << path << std::endl;
        std::cerr << "Upewnij sie ze katalog 'output/' istnieje." << std::endl;
        return;
    }

    f << "{\n";
    f << "  \"wojewodztwa\": {\n";

    for (size_t i = 0; i < woj.size(); i++) {
        const auto& w = woj[i];
        f << "    " << jsonStr(w.kod) << ": {\n";
        f << "      \"nazwa\": "        << jsonStr(w.nazwa)             << ",\n";
        f << "      \"avg_cena_m2\": "  << fmt(w.avg_cena_m2)          << ",\n";
        f << "      \"median_cena_m2\": " << fmt(w.median_cena_m2)     << ",\n";
        f << "      \"trend\": "        << fmt(w.trend)                 << ",\n";
        f << "      \"transakcje\": "   << w.liczba_transakcji          << ",\n";

        // Miasta dla tego województwa
        f << "      \"miasta\": [\n";
        if (miasta.count(w.kod)) {
            const auto& ms = miasta.at(w.kod);
            for (size_t j = 0; j < ms.size(); j++) {
                f << "        { \"nazwa\": " << jsonStr(ms[j].miasto)
                  << ", \"avg_cena_m2\": "   << fmt(ms[j].avg_cena_m2)
                  << ", \"transakcje\": "    << ms[j].liczba_transakcji << " }";
                if (j + 1 < ms.size()) f << ",";
                f << "\n";
            }
        }
        f << "      ]\n";
        f << "    }";
        if (i + 1 < woj.size()) f << ",";
        f << "\n";
    }

    f << "  }\n";
    f << "}\n";

    std::cout << "Zapisano: " << path << std::endl;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    std::string csvPath    = argc > 1 ? argv[1] : "../data/transakcje_rcn.csv";
    std::string outputPath = argc > 2 ? argv[2] : "../web/data.json";

    std::cout << "========================================================\n"
              << " EstateMap - Analiza danych\n"
              << " Wejscie: " << csvPath    << "\n"
              << " Wyjscie: " << outputPath << "\n"
              << "========================================================\n\n";

    // 1. Wczytaj dane
    auto records = loadCSV(csvPath);
    if (records.empty()) {
        std::cerr << "Brak danych!" << std::endl;
        return 1;
    }

    // 2. Analiza
    std::cout << "Analizuje..." << std::endl;
    auto wojStats    = analizujWoj(records);
    auto miastoStats = analizujMiasta(records, 10);

    // 3. Podsumowanie w terminalu
    std::cout << "\nWyniki per wojewodztwo:\n";
    std::cout << std::left
              << std::setw(24) << "Województwo"
              << std::setw(14) << "Avg cena/m²"
              << std::setw(14) << "Mediana"
              << std::setw(10) << "Trend"
              << "Transakcji\n";
    std::cout << std::string(72, '-') << "\n";

    // Sortuj po avg_cena_m2 malejąco
    std::sort(wojStats.begin(), wojStats.end(),
        [](const WojStats& a, const WojStats& b) {
            return a.avg_cena_m2 > b.avg_cena_m2;
        });

    for (const auto& w : wojStats) {
        std::cout << std::left
                  << std::setw(24) << w.nazwa
                  << std::setw(14) << (fmt(w.avg_cena_m2) + " zl/m2")
                  << std::setw(14) << (fmt(w.median_cena_m2) + " zl/m2")
                  << std::setw(10) << (fmt(w.trend, 1) + "%")
                  << w.liczba_transakcji << "\n";
    }

    // 4. Eksport JSON
    std::cout << "\nEksportuje JSON...\n";
    exportJSON(wojStats, miastoStats, outputPath);

    std::cout << "\n========================================================\n"
              << " Gotowe! Otworz web/index.html w przegladarce.\n"
              << "========================================================\n";
    return 0;
}