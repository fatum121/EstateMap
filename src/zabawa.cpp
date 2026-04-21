#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>

int main() {
 std::ifstream plik("/home/fatum/CLionProjects/EstateMap/cmake-build-debug/transakcje.csv");

 if (!plik.is_open()) {
  std::cout << "Nie mogę otworzyć pliku" << std::endl;
  return 1;
 }

 std::string line;

 while (std::getline(plik, line)) {
  std::stringstream ss(line);
  std::string word;
  std::vector<std::string> words;

  while (std::getline(ss, word, ',')) {
   words.push_back(word);
  }

  for (const auto& element : words) {
   std::cout << element << " ";
  }
  std::cout << std::endl;
 }

 return 0;
}
