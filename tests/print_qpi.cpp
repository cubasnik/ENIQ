#include <iostream>
#include <sqlite3.h>
#include <string>

int main(int argc, char** argv) {
    const char* dbfile = "qpi_test.db";
    const char* table = "qpi_hourly_results";
    if (argc > 1) dbfile = argv[1];
    if (argc > 2) table = argv[2];

    sqlite3* db = nullptr;
    if (sqlite3_open(dbfile, &db) != SQLITE_OK) {
        std::cerr << "Failed to open DB: " << dbfile << "\n";
        return 1;
    }

    std::string q = "SELECT mo_ldn, attempts, success, rrc_success_rate, interval_minutes FROM ";
    q += table;
    q += " ORDER BY mo_ldn, interval_minutes;";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, q.c_str(), -1, &st, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare query: " << sqlite3_errmsg(db) << "\n";
        sqlite3_close(db);
        return 2;
    }

    std::cout << "mo_ldn,attempts,success,rrc_success_rate,interval_minutes\n";
    while (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char* mo = sqlite3_column_text(st,0);
        double attempts = sqlite3_column_double(st,1);
        double success = sqlite3_column_double(st,2);
        if (sqlite3_column_type(st,3) == SQLITE_NULL) {
            std::cout << '"' << (mo? (const char*)mo : "(null)") << '",' << attempts << ',' << success << ',' << ",";
        } else {
            double rate = sqlite3_column_double(st,3);
            std::cout << '"' << (mo? (const char*)mo : "(null)") << '",' << attempts << ',' << success << ',' << rate << ',';
        }
        int im = sqlite3_column_int(st,4);
        std::cout << im << '\n';
    }

    sqlite3_finalize(st);
    sqlite3_close(db);
    return 0;
}
