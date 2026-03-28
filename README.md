# EstateMap
# Jakub Bieńkowski
## Analiza cen nieruchomości w Polsce z wykorzystaniem C++ oraz interaktywnej mapy

Projekt analizuje w czasie rzeczywistym publiczne dane o cenach nieruchomości w Polsce i prezentuje je na interaktywnej mapie.

---

# Funkcje projektu

## Analiza danych

* wczytywanie danych z plików CSV
* przetwarzanie dużych zbiorów danych
* obliczanie:

  * średniej ceny m²
  * mediany
  * trendu cen

## Agregacja danych

* średnia cena m² dla miasta
* średnia cena dla województwa
* ranking miast

## Interaktywna mapa

Mapa Polski pokazująca:

* kolor regionu zależny od ceny
* kliknięcie regionu → szczegóły

Przykład:

```
Mazowieckie
średnia cena: 14500 zł/m²
trend: +7%
```

## Eksport danych

Program C++ generuje plik:

```
data.json
```

który jest używany przez stronę HTML.

---

# Użyte technologie

## Backend / analiza danych

* C++
* STL (`vector`, `map`, `unordered_map`)
* parser CSV

## Wizualizacja

* HTML
* JavaScript
* Leaflet – biblioteka do map

---

# Dane publiczne

* Główny Urząd Statystyczny
* Narodowy Bank Polski
* Portal Dane.gov.pl

---

# Źródła danych

## Dane o cenach mieszkań

NBP
[https://nbp.pl/statystyka-i-sprawozdawczosc/rynek-nieruchomosci/](https://nbp.pl/statystyka-i-sprawozdawczosc/rynek-nieruchomosci/)

GUS
[https://stat.gov.pl](https://stat.gov.pl)

Portal open data
[https://dane.gov.pl](https://dane.gov.pl)

---

# Dane mapy Polski (GeoJSON)

Potrzebne do wyświetlenia mapy.

Przykłady repozytoriów:

[https://github.com/gregoiredavid/france-geojson](https://github.com/gregoiredavid/france-geojson)

Polska:

[https://github.com/johan/world.geo.json](https://github.com/johan/world.geo.json)

lub

[https://geojson-maps.ash.ms/](https://geojson-maps.ash.ms/)

---

# Inspiracje / podobne projekty

* Tableau
* Power BI
* przykłady map Leaflet

---

# Struktura projektu

```
real-estate-project

README.md

data/
    raw_data.csv

src/
    main.cpp
    csv_parser.cpp
    csv_parser.h
    analysis.cpp
    analysis.h
    export_json.cpp
    export_json.h

output/
    data.json

web/
    index.html
    map.js
    style.css
    poland.geojson
```

---

# Harmonogram projektu (12 tygodni)

---

# Tydzień 1–2

## Cel

Przygotowanie środowiska i danych.

## Co muszę umieć

* kompilacja C++
* podstawy STL
* czytanie plików

```
ifstream
vector
string
```

## Zadania

* znaleźć dataset (np. NBP)
* pobrać dane
* przygotować strukturę projektu
* napisać prosty program:

  * read CSV
  * print data

## Deliverables

* repozytorium Git
* przykładowy dataset
* program wczytujący CSV

---

# Tydzień 3–4

## Cel

Parser danych.

## Co muszę umieć

* string parsing
* stringstream
* vector
* struktury

## Struktura danych

```cpp
struct PropertyData
{
    string city;
    int year;
    double price;
};
```

## Zadania

Napisać funkcję:

```
loadCSV()
```

która:

* czyta plik
* zapisuje dane do `vector`

## Deliverables

* parser CSV
* wczytanie danych do pamięci
* test na kilku plikach

---

# Tydzień 5–6

## Cel

Analiza danych.

## Co muszę umieć

* map
* unordered_map
* algorytmy STL

## Funkcje analizy

```
averagePrice(city)
averagePrice(region)
maxPrice()
minPrice()
```

## Dodatkowo

Ranking miast:

```
sort cities by price
```

## Deliverables

* moduł `analysis.cpp`
* ranking miast
* statystyki

---

# Tydzień 7–8

## Cel

Eksport danych.

C++ generuje JSON dla mapy.

## Co muszę umieć

* format JSON
* zapisywanie plików

## Format JSON

```json
{
  "Mazowieckie": 14500,
  "Malopolskie": 12000,
  "Slaskie": 9000
}
```

## Zadania

Napisać moduł:

```
export_to_json()
```

## Deliverables

* plik `data.json`
* poprawne dane dla województw

---

# Tydzień 9–10

## Cel

Interaktywna mapa.

## Co muszę umieć

* podstawy HTML
* podstawy JavaScript

## Biblioteka

Leaflet
[https://leafletjs.com](https://leafletjs.com)

## Zadania

* stworzyć stronę `index.html`
* wczytać `poland.geojson`
* pokolorować regiony

## Deliverables

* mapa Polski
* regiony kolorowane ceną

---

# Tydzień 11–12

## Cel

Testy i poprawki.

(2 tygodnie zostawione zgodnie z wymaganiami)

## Zadania

Testy:

* różne dataset
* brakujące dane
* duże pliki

Poprawki:

* optymalizacja
* obsługa błędów

## Deliverables

* finalna wersja
* README
* instrukcja uruchomienia

---

# Co muszę umieć do projektu

## C++

* struktury
* vector
* map
* sortowanie
* file I/O

## Web

* HTML
* podstawy JS
* Leaflet

---

# Jak projekt będzie działał

Pipeline projektu:

```
DATASET
   ↓
C++ ANALIZA
   ↓
JSON
   ↓
HTML + JS
   ↓
INTERAKTYWNA MAPA
```
