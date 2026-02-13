// all_in_one.cpp — весь проект в одном файле

#include <windows.h>      // SetConsoleOutputCP
#include <iostream>       // cout, cerr
#include <string>         // string
#include <vector>         // vector
#include <unordered_set>  // unordered_set
#include <sstream>        // istringstream
#include <filesystem>     // filesystem

namespace fs = std::filesystem;

// Если хочешь использовать std::cout без префикса std:: — оставь эту строку
using namespace std;

// === Остальной код ===
// struct CounterRecord { ...
// bool parse_ericssonsoft_pm_xml(...
// bool initDatabase(...
// bool saveRecords(...
// int main(...
// pugixml.cpp ...

using namespace std;     // чтобы не писать std::cout каждый раз (можно убрать, если хочешь использовать std::cout)
int main(int argc, char* argv[]) {
    SetConsoleOutputCP(65001);  // UTF-8 для консоли

    if (argc < 2) {
        std::cout << "Использование: eniq <путь_к_xml_или_папке>\n";
        return 1;
    }

    // ... весь остальной код main() из твоего src/main.cpp ...
    // включая обработку папки, вызов parse_ericssonsoft_pm_xml() и saveRecords()

    return 0;
}