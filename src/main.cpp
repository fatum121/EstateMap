#include "gdal_priv.h"
#include "ogrsf_frmts.h"
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>

#ifdef _WIN32
    #define POBIERZ "powershell -Command \"Invoke-WebRequest -Uri '%s' -OutFile 'dane.zip'\""
    #define ROZPAKUJ "powershell -Command \"Expand-Archive -Path 'dane.zip' -DestinationPath 'dane' -Force\""
#else
    #define POBIERZ "wget -O dane.zip '%s'"
    #define ROZPAKUJ "unzip -o dane.zip -d dane"
#endif

int main() {
    // --- 1. Pobieranie i rozpakowywanie ---
    std::string url = "https://opendata.geoportal.gov.pl/InneDane/latest_exports/rcn_transakcje_ceny/GPKG/1605_transakcje_ceny.gpkg.zip";

    std::cout << "1. Pobieranie pliku..." << std::endl;
    char pobierz[1024];
    snprintf(pobierz, sizeof(pobierz), POBIERZ, url.c_str());
    system(pobierz);  // <-- USUNĄŁEM .c_str() - pobierz jest już tablicą char

    std::cout << "2. Rozpakowywanie..." << std::endl;
    system(ROZPAKUJ);

    // --- 2. Inicjalizacja GDAL ---
    GDALAllRegister();

    // --- 3. Ścieżka do rozpakowanego GPKG ---
    std::string sciezka_do_gpkg = "dane/1605_transakcje_ceny.gpkg";
    std::string plik_csv = "transakcje.csv";

    std::cout << "3. Przetwarzanie pliku GPKG..." << std::endl;

    // Otwórz plik GeoPackage
    GDALDataset* poDS = (GDALDataset*)GDALOpenEx(sciezka_do_gpkg.c_str(), GDAL_OF_VECTOR, NULL, NULL, NULL);
    if (poDS == NULL) {
        std::cerr << "Nie można otworzyć pliku GPKG!" << std::endl;
        return 1;
    }

    // Weź pierwszą warstwę
    OGRLayer* poLayer = poDS->GetLayer(0);
    if (poLayer == NULL) {
        std::cerr << "Nie znaleziono warstwy!" << std::endl;
        GDALClose(poDS);
        return 1;
    }

    // Otwórz plik CSV
    std::ofstream csv_file(plik_csv);
    if (!csv_file.is_open()) {
        std::cerr << "Nie można utworzyć pliku CSV!" << std::endl;
        GDALClose(poDS);
        return 1;
    }

    // Zapisz nagłówki kolumn
    OGRFeatureDefn* poFDefn = poLayer->GetLayerDefn();
    for (int i = 0; i < poFDefn->GetFieldCount(); i++) {
        csv_file << poFDefn->GetFieldDefn(i)->GetNameRef();
        if (i < poFDefn->GetFieldCount() - 1) csv_file << ",";
    }
    csv_file << std::endl;

    // Główna pętla - zapis danych
    OGRFeature* poFeature;
    int licznik = 0;
    while ((poFeature = poLayer->GetNextFeature()) != NULL) {
        for (int i = 0; i < poFDefn->GetFieldCount(); i++) {
            const char* wartosc = poFeature->GetFieldAsString(i);
            csv_file << (wartosc ? wartosc : "NULL");
            if (i < poFDefn->GetFieldCount() - 1) csv_file << ",";
        }
        csv_file << std::endl;
        OGRFeature::DestroyFeature(poFeature);
        licznik++;
    }

    // Sprzątanie
    csv_file.close();
    GDALClose(poDS);

    std::cout << "4. Gotowe! Zapisano " << licznik << " rekordów do pliku: " << plik_csv << std::endl;

    return 0;
}