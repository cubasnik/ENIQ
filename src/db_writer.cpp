#include "db_writer.h"
#include <sqlite3.h>
#include <iostream>
#include <unordered_set>
#include <string>

// default language flag (can be set by main)
bool g_use_russian = false;

static int callback(void* NotUsed, int argc, char** argv, char** azColName) {
    return 0;
}

bool initDatabase(const std::string& dbPath) {
    sqlite3* db;
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc) {
        if (g_use_russian) std::cerr << "Ошибка открытия БД: " << (db ? sqlite3_errmsg(db) : "(no handle)") << "\n";
        else std::cerr << "DB open error: " << (db ? sqlite3_errmsg(db) : "(no handle)") << "\n";
        if (db) sqlite3_close(db);
        return false;
    }

    const char* sql =
        "CREATE TABLE IF NOT EXISTS pm_counters ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  timestamp TEXT NOT NULL,"
        "  mo_ldn TEXT NOT NULL,"
        "  meas_type TEXT,"
        "  counter_name TEXT NOT NULL,"
        "  value REAL"
        ");";

    char* errMsg = nullptr;
    rc = sqlite3_exec(db, sql, callback, 0, &errMsg);
    if (rc != SQLITE_OK) {
        if (g_use_russian) std::cerr << "SQL ошибка: " << (errMsg ? errMsg : "") << "\n";
        else std::cerr << "SQL error: " << (errMsg ? errMsg : "") << "\n";
        if (errMsg) sqlite3_free(errMsg);
    }

    // Ensure uniqueness to prevent duplicates: (timestamp, mo_ldn, meas_type, counter_name)
    const char* idx_sql =
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_unique_pm ON pm_counters (timestamp, mo_ldn, meas_type, counter_name);";
    rc = sqlite3_exec(db, idx_sql, callback, 0, &errMsg);
    if (rc != SQLITE_OK) {
        if (g_use_russian) std::cerr << "Ошибка индекса SQL: " << (errMsg ? errMsg : "") << "\n";
        else std::cerr << "SQL index error: " << (errMsg ? errMsg : "") << "\n";
        if (errMsg) sqlite3_free(errMsg);
    }

    sqlite3_close(db);
    return true;
}

bool saveRecords(const std::string& dbPath, const std::vector<CounterRecord>& records) {
    if (records.empty()) return true;

    // In-memory dedupe to reduce DB work
    std::unordered_set<std::string> seen;
    std::vector<CounterRecord> filtered;
    filtered.reserve(records.size());

    for (const auto& r : records) {
        std::string key = r.timestamp + "\x1" + r.mo_ldn + "\x1" + r.meas_type + "\x1" + r.counter_name;
        if (seen.insert(key).second) filtered.push_back(r);
    }

    if (filtered.empty()) {
        if (g_use_russian) std::cout << "Нет новых записей для сохранения\n";
        else std::cout << "No new records to save\n";
        return true;
    }

    sqlite3* db = nullptr;
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc) {
        if (g_use_russian) std::cerr << "Ошибка открытия БД: " << (db ? sqlite3_errmsg(db) : "(no handle)") << "\n";
        else std::cerr << "DB open error: " << (db ? sqlite3_errmsg(db) : "(no handle)") << "\n";
        if (db) sqlite3_close(db);
        return false;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    sqlite3_stmt* stmt;
    // Use INSERT OR IGNORE to let unique index filter duplicates
    const char* sql_ins = "INSERT OR IGNORE INTO pm_counters (timestamp, mo_ldn, meas_type, counter_name, value) VALUES (?, ?, ?, ?, ?);";
    rc = sqlite3_prepare_v2(db, sql_ins, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        if (g_use_russian) std::cerr << "Ошибка подготовки запроса: " << sqlite3_errmsg(db) << "\n";
        else std::cerr << "Prepare error: " << sqlite3_errmsg(db) << "\n";
        sqlite3_close(db);
        return false;
    }

    for (const auto& r : filtered) {
        sqlite3_bind_text(stmt, 1, r.timestamp.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, r.mo_ldn.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, r.meas_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, r.counter_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 5, r.value);

        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    sqlite3_close(db);

    if (g_use_russian) std::cout << "Сохранено " << filtered.size() << " новых записей (из " << records.size() << ")\n";
    else std::cout << "Saved " << filtered.size() << " new records (of " << records.size() << ")\n";
    return true;
}


