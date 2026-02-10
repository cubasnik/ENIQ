#include "xml_parser.h"
#include "db_writer.h"
#include <iostream>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Использование: eniq <путь_к_xml_или_папке>\n";
        return 1;
    }

    std::string path = argv[1];
    std::vector<CounterRecord> all;

    const std::string db = "eniq_data.db";

    if (!initDatabase(db)) return 1;

    if (fs::is_directory(path)) {
        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.is_regular_file() && entry.path().extension() == ".xml") {
                std::cout << "Обработка: " << entry.path().filename() << "\n";
                std::vector<CounterRecord> recs;
                if (parse_ericsson_pm_xml(entry.path().string(), recs)) {
                    saveRecords(db, recs);
                    all.insert(all.end(), recs.begin(), recs.end());
                }
            }
        }
    } else if (fs::exists(path) && fs::path(path).extension() == ".xml") {
        std::vector<CounterRecord> recs;
        if (parse_ericsson_pm_xml(path, recs)) {
            saveRecords(db, recs);
        }
    }

    std::cout << "Готово. Данные в " << db << "\n";
    return 0;
}