#include "../src/xml_parser.h"
#include <iostream>
#include <vector>

int main() {
    std::vector<CounterRecord> recs;
    bool ok = parse_ericsson_pm_xml("data/test.xml", recs);
    if (!ok) {
        std::cerr << "parse failed\n";
        return 2;
    }
    if (recs.size() != 2) {
        std::cerr << "unexpected record count: " << recs.size() << "\n";
        return 3;
    }
    std::cout << "OK\n";
    return 0;
}
