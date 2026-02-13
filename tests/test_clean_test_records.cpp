#include <iostream>
#include <vector>
#include <cstdio>
#include "xml_parser.h"
#include "db_writer.h"
#include <sqlite3.h>
#include <cassert>

int main() {
    const std::string db = "test_clean.db";
    // remove any previous test DB
    std::remove(db.c_str());

    if (!initDatabase(db)) {
        std::cerr << "initDatabase failed\n";
        return 1;
    }

    sqlite3* conn = nullptr;
    int rc = sqlite3_open(db.c_str(), &conn);
    if (rc != SQLITE_OK) {
        std::cerr << "sqlite open failed\n";
        return 1;
    }

    const char* sql = "INSERT INTO pm_counters (timestamp, mo_ldn, meas_type, counter_name, value) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "prepare failed\n";
        sqlite3_close(conn);
        return 1;
    }

    auto insert = [&](const char* ts, const char* mo, const char* mt, const char* cn, double v){
        sqlite3_bind_text(stmt, 1, ts, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, mo, -1, SQLITE_TRANSIENT);
        if (mt) sqlite3_bind_text(stmt, 3, mt, -1, SQLITE_TRANSIENT); else sqlite3_bind_null(stmt, 3);
        sqlite3_bind_text(stmt, 4, cn, -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 5, v);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    };

    // Normal record
    insert("2020-01-01T00:00:00Z", "MO1", "MT_OK", "ctr_ok", 1.23);
    // Test-like records
    insert("2020-01-01T00:01:00Z", "MO1", "MT1", "unk_test1", 5.0);
    insert("2020-01-01T00:02:00Z", "MO1", nullptr, "ctr_no_meas", 2.0); // meas_type NULL
    insert("2020-01-01T00:03:00Z", "MO1", "MT2", "ctr_zero", 0.0); // value == 0

    sqlite3_finalize(stmt);
    sqlite3_close(conn);

    // Dry-run
    std::vector<CounterRecord> candidates;
    bool ok = dryRunCleanTestRecords(db, candidates);
    if (!ok) {
        std::cerr << "dryRunCleanTestRecords failed\n";
        return 1;
    }
    if (candidates.size() != 3) {
        std::cerr << "Expected 3 candidates, got " << candidates.size() << "\n";
        return 1;
    }

    // Actual removal
    if (!cleanTestRecords(db)) {
        std::cerr << "cleanTestRecords failed\n";
        return 1;
    }

    // Verify only one remains
    conn = nullptr;
    rc = sqlite3_open(db.c_str(), &conn);
    if (rc != SQLITE_OK) {
        std::cerr << "sqlite open failed\n";
        return 1;
    }
    sqlite3_stmt* cnt_stmt = nullptr;
    rc = sqlite3_prepare_v2(conn, "SELECT COUNT(*) FROM pm_counters;", -1, &cnt_stmt, nullptr);
    if (rc != SQLITE_OK) { sqlite3_close(conn); return 1; }
    rc = sqlite3_step(cnt_stmt);
    int cnt = 0;
    if (rc == SQLITE_ROW) cnt = sqlite3_column_int(cnt_stmt, 0);
    sqlite3_finalize(cnt_stmt);
    sqlite3_close(conn);

    if (cnt != 1) {
        std::cerr << "Expected 1 remaining row, got " << cnt << "\n";
        return 1;
    }

    // cleanup
    std::remove(db.c_str());
    std::cout << "clean test passed\n";
    return 0;
}
