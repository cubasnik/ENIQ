#include "xml_parser.h"
#include <iostream>

int main() {
    std::vector<CounterRecord> recs;
    bool ok = parse_ericssonsoft_pm_xml("..\\data\\test_p_attr.xml.gz", recs);
    if (!ok) {
        std::cerr << "parse_ericssonsoft_pm_xml failed for gz input\n";
        return 1;
    }
    if (recs.empty()) {
        std::cerr << "No records parsed from gz sample\n";
        return 1;
    }
    // Expect at least three records (ctr1, ctr2, ctr_csv)
    bool found_ctr1 = false, found_ctr2 = false, found_ctr_csv = false;
    for (const auto &r : recs) {
        if (r.counter_name == "ctr1") found_ctr1 = true;
        if (r.counter_name == "ctr2") found_ctr2 = true;
        if (r.counter_name == "ctr_csv") found_ctr_csv = true;
    }
    if (!found_ctr1 || !found_ctr2 || !found_ctr_csv) {
        std::cerr << "Missing expected counters in gz parse result\n";
        return 1;
    }
    std::cout << "gz parser test OK\n";
    return 0;
}
