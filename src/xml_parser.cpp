#include "xml_parser.h"
#include <pugixml.hpp>
#include <sstream>
#include <iostream>

bool parse_ericsson_pm_xml(const std::string& xmlPath, std::vector<CounterRecord>& records) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(xmlPath.c_str(), pugi::parse_default | pugi::parse_trim_pcdata);

    if (!result) {
        std::cerr << "Ошибка XML: " << result.description() << " в файле " << xmlPath << "\n";
        return false;
    }

    std::string ts;
    pugi::xpath_node colNode = doc.select_node("//measCollec");
    if (colNode) {
        pugi::xml_node mc = colNode.node();
        if (mc) {
            pugi::xml_attribute a = mc.attribute("beginTime");
            if (a) ts = a.as_string();
        }
    }

    for (pugi::xpath_node xn : doc.select_nodes("//measInfo")) {
        pugi::xml_node measInfo = xn.node();
        std::string measId = measInfo.attribute("measInfoId").as_string();

        // Список имён счётчиков
        std::vector<std::string> counters;
        pugi::xml_node typesNode = measInfo.child("measTypes");
        if (typesNode) {
            std::istringstream iss(typesNode.text().as_string());
            std::string token;
            while (iss >> token) counters.push_back(token);
        }

        for (pugi::xml_node val : measInfo.children("measValue")) {
            std::string mo = val.child("measObjLdn").text().as_string();

            size_t idx = 0;
            for (pugi::xml_node r : val.children("r")) {
                double v = r.text().as_double(0.0);
                std::string name = (idx < counters.size()) ? counters[idx] : ("unk_" + std::to_string(idx));
                records.push_back({ts, mo, measId, name, v});
                ++idx;
            }
        }
    }

    return true;
}
// Only `parseEricssonPM` is provided; duplicate alternative implementations
// were removed to avoid redundancy and conflicting declarations.
