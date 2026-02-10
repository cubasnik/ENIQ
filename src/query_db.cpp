#include <sqlite3.h>
#include <iostream>

int main() {
    sqlite3* db = nullptr;
    if (sqlite3_open("eniq_data.db", &db) != SQLITE_OK) {
        std::cerr << "Ошибка открытия eniq_data.db\n";
        return 1;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* qcount = "SELECT COUNT(*) FROM pm_counters;";
    if (sqlite3_prepare_v2(db, qcount, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int cnt = sqlite3_column_int(stmt, 0);
            std::cout << "rows_count: " << cnt << "\n";
        }
        sqlite3_finalize(stmt);
    }

    const char* q = "SELECT timestamp, mo_ldn, meas_type, counter_name, value FROM pm_counters LIMIT 10;";
    if (sqlite3_prepare_v2(db, q, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* ts = sqlite3_column_text(stmt, 0);
            const unsigned char* mo = sqlite3_column_text(stmt, 1);
            const unsigned char* mt = sqlite3_column_text(stmt, 2);
            const unsigned char* cn = sqlite3_column_text(stmt, 3);
            double v = sqlite3_column_double(stmt, 4);
            std::cout << (ts ? (const char*)ts : "NULL") << ", "
                      << (mo ? (const char*)mo : "NULL") << ", "
                      << (mt ? (const char*)mt : "NULL") << ", "
                      << (cn ? (const char*)cn : "NULL") << ", " << v << "\n";
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return 0;
}
