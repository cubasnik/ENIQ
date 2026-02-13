#include "../src/xml_parser.h"
#include <iostream>
#include <vector>

int main() {
    std::vector<CounterRecord> recs;
    bool ok = parse_ericssonsoft_pm_xml("data/test_ns.xml", recs);
    if (!ok) {
        std::cerr << "parse failed\n";
        return 2;
    }
    if (recs.size() != 2) {
        std::cerr << "unexpected record count: " << recs.size() << "\n";
        return 3;
    }
    // Verify values and names
    if (recs[0].counter_name != "cnt1" || fabs(recs[0].value - 1.0) > 1e-6) {
        std::cerr << "first record mismatch\n";
        return 4;
    }
    if (recs[1].counter_name != "cnt2" || fabs(recs[1].value - 2.0) > 1e-6) {
        std::cerr << "second record mismatch\n";
        return 5;
    }
    std::cout << "OK\n";
    return 0;
}
