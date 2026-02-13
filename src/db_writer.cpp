#include "db_writer.h"
#include "logger.h"
#include <sqlite3.h>
#include <cstdio>
#include <iostream>
#include <unordered_set>
#include <string>
#include "xml_parser.h"

// default language flag (can be set by main)
bool g_use_russian = false;

static int callback(void* NotUsed, int argc, char** argv, char** azColName) {
    return 0;
}

bool initDatabase(const std::string& dbPath) {
    sqlite3* db;
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc) {
        std::string msg = std::string(db ? sqlite3_errmsg(db) : "(no handle)");
        if (g_use_russian) logger::error(std::string("Ошибка открытия БД: ") + msg);
        else logger::error(std::string("DB open error: ") + msg);
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
        std::string msg = errMsg ? errMsg : std::string();
        if (g_use_russian) logger::error(std::string("SQL ошибка: ") + msg);
        else logger::error(std::string("SQL error: ") + msg);
        if (errMsg) sqlite3_free(errMsg);
    }

    // Ensure uniqueness to prevent duplicates: (timestamp, mo_ldn, meas_type, counter_name)
    const char* idx_sql =
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_unique_pm ON pm_counters (timestamp, mo_ldn, meas_type, counter_name);";
    rc = sqlite3_exec(db, idx_sql, callback, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::string msg = errMsg ? errMsg : std::string();
        if (g_use_russian) logger::error(std::string("Ошибка индекса SQL: ") + msg);
        else logger::error(std::string("SQL index error: ") + msg);
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
        if (g_use_russian) logger::info("Нет новых записей для сохранения");
        else logger::info("No new records to save");
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
        if (g_use_russian) logger::error(std::string("Ошибка подготовки запроса: ") + sqlite3_errmsg(db));
        else logger::error(std::string("Prepare error: ") + sqlite3_errmsg(db));
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

    if (g_use_russian) logger::info(std::string("Сохранено ") + std::to_string(filtered.size()) + " новых записей (из " + std::to_string(records.size()) + ")");
    else logger::info(std::string("Saved ") + std::to_string(filtered.size()) + " new records (of " + std::to_string(records.size()) + ")");
    return true;
}

bool cleanTestRecords(const std::string& dbPath)
{
    sqlite3* db = nullptr;
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc != SQLITE_OK) {
        logger::error(std::string("cleanTestRecords: unable to open DB: ") + (db ? sqlite3_errmsg(db) : "(no handle)"));
        if (db) sqlite3_close(db);
        return false;
    }

    const char* sql =
        "BEGIN TRANSACTION;"
        "DELETE FROM pm_counters WHERE counter_name LIKE 'unk_%' OR meas_type IS NULL OR trim(meas_type) = '' OR value = 0;"
        "COMMIT;";

    char* err = nullptr;
    rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        logger::error(std::string("cleanTestRecords: SQL error: ") + (err ? err : "(unknown)"));
        if (err) sqlite3_free(err);
        sqlite3_close(db);
        return false;
    }

    int deleted = sqlite3_changes(db);
    sqlite3_close(db);
    logger::info(std::string("cleanTestRecords: removed ") + std::to_string(deleted) + " test/unusual records from DB: " + dbPath);
    return true;
}

bool dryRunCleanTestRecords(const std::string& dbPath, std::vector<CounterRecord>& out)
{
    out.clear();
    sqlite3* db = nullptr;
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc != SQLITE_OK) {
        logger::error(std::string("dryRunCleanTestRecords: unable to open DB: ") + (db ? sqlite3_errmsg(db) : "(no handle)"));
        if (db) sqlite3_close(db);
        return false;
    }

    const char* sql = "SELECT timestamp, mo_ldn, meas_type, counter_name, value FROM pm_counters WHERE counter_name LIKE 'unk_%' OR meas_type IS NULL OR trim(meas_type) = '' OR value = 0;";
    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        logger::error(std::string("dryRunCleanTestRecords: prepare failed: ") + sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        CounterRecord r;
        const unsigned char* t = sqlite3_column_text(stmt, 0);
        const unsigned char* m = sqlite3_column_text(stmt, 1);
        const unsigned char* mt = sqlite3_column_text(stmt, 2);
        const unsigned char* cn = sqlite3_column_text(stmt, 3);
        double v = sqlite3_column_double(stmt, 4);
        r.timestamp = t ? reinterpret_cast<const char*>(t) : std::string();
        r.mo_ldn = m ? reinterpret_cast<const char*>(m) : std::string();
        r.meas_type = mt ? reinterpret_cast<const char*>(mt) : std::string();
        r.counter_name = cn ? reinterpret_cast<const char*>(cn) : std::string();
        r.value = v;
        out.push_back(r);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    logger::info(std::string("dryRunCleanTestRecords: found ") + std::to_string(out.size()) + " candidate records");
    return true;
}


