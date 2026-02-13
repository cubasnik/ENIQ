#include "../src/xml_parser.h"
#include "../src/db_writer.h"
#include <iostream>
#include <vector>
#include <sqlite3.h>
#include <cstdio>

int main() {
    std::vector<CounterRecord> recs;
    bool ok = parse_ericssonsoft_pm_xml("data/test.xml", recs);
    if (!ok) {
        std::cerr << "parse failed\n";
        return 2;
    }
    if (recs.size() != 2) {
        std::cerr << "unexpected record count: " << recs.size() << "\n";
        return 3;
    }

    const std::string dbPath = "eniq_test.db";
    std::remove(dbPath.c_str());
    if (!initDatabase(dbPath)) { std::cerr << "initDatabase failed\n"; return 4; }
    if (!saveRecords(dbPath, recs)) { std::cerr << "saveRecords failed\n"; return 5; }

    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) { std::cerr << "sqlite open failed\n"; if (db) sqlite3_close(db); return 6; }
    // Verify specific rows for cnt1 and cnt2
    auto check_counter = [&](const std::string& cname, double expected_value) -> bool {
        sqlite3_stmt* s = nullptr;
        const char* q2 = "SELECT timestamp, mo_ldn, meas_type, counter_name, value FROM pm_counters WHERE counter_name = ? LIMIT 1;";
        if (sqlite3_prepare_v2(db, q2, -1, &s, nullptr) != SQLITE_OK) { return false; }
        if (sqlite3_bind_text(s, 1, cname.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) { sqlite3_finalize(s); return false; }
        int r = sqlite3_step(s);
        if (r != SQLITE_ROW) { sqlite3_finalize(s); return false; }
        const unsigned char* ts_col = sqlite3_column_text(s, 0);
        const unsigned char* mo_col = sqlite3_column_text(s, 1);
        const unsigned char* meas_col = sqlite3_column_text(s, 2);
        const unsigned char* name_col = sqlite3_column_text(s, 3);
        double val = sqlite3_column_double(s, 4);
        std::string ts = ts_col ? reinterpret_cast<const char*>(ts_col) : std::string();
        std::string mo = mo_col ? reinterpret_cast<const char*>(mo_col) : std::string();
        std::string meas = meas_col ? reinterpret_cast<const char*>(meas_col) : std::string();
        std::string name = name_col ? reinterpret_cast<const char*>(name_col) : std::string();
        sqlite3_finalize(s);
        if (mo != "MO1") return false;
        if (meas != "m1") return false;
        if (name != cname) return false;
        if (fabs(val - expected_value) > 1e-6) return false;
        if (ts != "2026-02-10T00:00:00Z") return false;
        return true;
    };

    bool ok1 = check_counter("cnt1", 1.0);
    bool ok2 = check_counter("cnt2", 2.0);
    sqlite3_close(db);

    // Cleanup test DB
    std::remove(dbPath.c_str());

    if (!ok1) { std::cerr << "cnt1 row mismatch\n"; return 9; }
    if (!ok2) { std::cerr << "cnt2 row mismatch\n"; return 10; }

    std::cout << "OK\n";
    return 0;
}
