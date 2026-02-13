#include "xml_parser.h"
#include "db_writer.h"
#include "logger.h"
#include <pugixml.hpp>
#include <sstream>
#include <iostream>
#include <map>
#include <locale>
#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

static std::string local_name(const char* qname) {
    if (!qname) return std::string();
    const char* p = std::strrchr(qname, ':');
    return p ? std::string(p + 1) : std::string(qname);
}

bool parse_ericssonsoft_pm_xml(const std::string& xmlPath, std::vector<CounterRecord>& records) {
    pugi::xml_document doc;

    pugi::xml_parse_result result;
    bool is_gz = (xmlPath.size() >= 3 && xmlPath.substr(xmlPath.size() - 3) == ".gz");
#ifdef HAVE_ZLIB
    auto load_gz_to_string = [](const std::string &path, std::string &out) -> bool {
        gzFile f = gzopen(path.c_str(), "rb");
        if (!f) return false;
        char buf[8192];
        int n = 0;
        out.clear();
        size_t total = 0;
        while ((n = gzread(f, buf, sizeof(buf))) > 0) { out.append(buf, n); total += (size_t)n; }
        int gzerr = gzclose(f);
        if (gzerr != Z_OK) {
            logger::error(std::string("gzip read error (gzclose returned ") + std::to_string(gzerr) + ") for " + path);
            return false;
        }
        logger::debug(std::string("Read ") + std::to_string(total) + " bytes from gz file " + path);
        return true;
    };

    if (is_gz) {
        std::string data;
        if (!load_gz_to_string(xmlPath, data)) {
            logger::error(std::string("Ошибка чтения gzip: ") + xmlPath);
            return false;
        }
        result = doc.load_buffer(data.c_str(), data.size(), pugi::parse_default | pugi::parse_trim_pcdata);
    } else {
        result = doc.load_file(xmlPath.c_str(), pugi::parse_default | pugi::parse_trim_pcdata);
    }
#else
    if (is_gz) {
        logger::error(std::string("gzip support not available (build without zlib): ") + xmlPath);
        return false;
    }
    result = doc.load_file(xmlPath.c_str(), pugi::parse_default | pugi::parse_trim_pcdata);
#endif

    if (!result) {
        std::cerr << "Ошибка XML: " << result.description() << " в файле " << xmlPath << "\n";
        return false;
    }

    std::string ts;
    pugi::xpath_node colNode = doc.select_node("//measCollec");
    if (colNode) {
        pugi::xml_attribute a = colNode.node().attribute("beginTime");
        if (a) ts = a.as_string();
    }

    // Проходим по всем measInfo
    for (pugi::xpath_node xn : doc.select_nodes("//measInfo")) {
        pugi::xml_node measInfo = xn.node();
        std::string measId = measInfo.attribute("measInfoId").as_string();

        // Карта: номер p → имя counter
        std::map<int, std::string> counterMap;
        for (pugi::xml_node typeNode : measInfo.children("measType")) {
            int p = typeNode.attribute("p").as_int(0);
            std::string name = typeNode.text().as_string();
            if (p > 0 && !name.empty()) {
                counterMap[p] = name;
            }
        }

        // Проходим по measValue
        for (pugi::xml_node val : measInfo.children("measValue")) {
            std::string mo = val.child("measObjLdn").text().as_string();

            // Проходим по всем <r> внутри measValue
            for (pugi::xml_node r : val.children("r")) {
                int p = r.attribute("p").as_int(0);
                std::string valueStr = r.text().as_string();

                if (p > 0) {
                    auto it = counterMap.find(p);
                    std::string counterName = (it != counterMap.end()) ? it->second : ("unk_p" + std::to_string(p));

                    // Обрабатываем множественные значения через запятую
                    std::stringstream ss(valueStr);
                    std::string token;
                    while (std::getline(ss, token, ',')) {
                        // Убираем пробелы
                        token.erase(0, token.find_first_not_of(" \t"));
                        token.erase(token.find_last_not_of(" \t") + 1);

                        if (token.empty() || token == " ") continue; // Пропускаем пустые

                        double v = 0.0;
                        {
                            std::istringstream tss(token);
                            tss.imbue(std::locale::classic());
                            if (!(tss >> v)) {
                                v = 0.0; // Если не число — 0
                            }
                        }

                        records.push_back({ts, mo, measId, counterName, v});
                    }
                }
            }
        }
    }

    return true;
}
