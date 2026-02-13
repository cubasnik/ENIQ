#include <sqlite3.h>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstring>

namespace fs = std::filesystem;

static void usage() {
    std::cout << "Usage: query_db [--db=path] [--qpi] [--start=ISO] [--end=ISO] [--attempt-name=name] [--success-name=name] [--interval-minutes=N] [--out-csv=path]\n";
    std::cout << "Usage: query_db [--db=path] [--qpi] [--start=ISO] [--end=ISO] [--attempt-name=name] [--success-name=name] [--interval-minutes=N] [--out-csv=path] [--save-to-db] [--save-table=name] [--config=path] [--create-indexes|--drop-indexes|--rebuild-indexes]\n";
}

int main(int argc, char** argv) {
    std::string dbpath = "eniq_data.db";
    bool do_qpi = false;
    std::string start_ts, end_ts;
    std::string attempt_name = "rrcConnAttempt";
    std::string success_name = "rrcConnSuccess";
    int interval_minutes = 0; // 0 = aggregate whole range
    std::string out_csv;
    bool save_to_db = false;
    std::string save_table = "qpi_results";
    std::string config_file;
    bool create_indexes_flag = false;
    bool drop_indexes_flag = false;
    bool rebuild_indexes_flag = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--db=",0) == 0) dbpath = a.substr(5);
        else if (a == "--qpi") do_qpi = true;
        else if (a.rfind("--start=",0) == 0) start_ts = a.substr(8);
        else if (a.rfind("--end=",0) == 0) end_ts = a.substr(6);
        else if (a.rfind("--attempt-name=",0) == 0) attempt_name = a.substr(15);
        else if (a.rfind("--success-name=",0) == 0) success_name = a.substr(15);
        else if (a.rfind("--interval-minutes=",0) == 0) interval_minutes = std::stoi(a.substr(18));
        else if (a.rfind("--out-csv=",0) == 0) out_csv = a.substr(10);
        else if (a == "--save-to-db") save_to_db = true;
        else if (a.rfind("--save-table=",0) == 0) save_table = a.substr(13);
        else if (a.rfind("--config=",0) == 0) config_file = a.substr(9);
        else if (a == "--create-indexes") create_indexes_flag = true;
        else if (a == "--drop-indexes") drop_indexes_flag = true;
        else if (a == "--rebuild-indexes") rebuild_indexes_flag = true;
        else if (a == "--help" || a == "-h") { usage(); return 0; }
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(dbpath.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Error opening DB: " << dbpath << "\n";
        return 1;
    }

    // Load config file (simple key=value pairs) if provided
    if (!config_file.empty()) {
        try {
            std::ifstream cf(config_file);
            if (cf) {
                std::string line;
                while (std::getline(cf, line)) {
                    auto pos = line.find('=');
                    if (pos == std::string::npos) continue;
                    std::string k = line.substr(0,pos);
                    std::string v = line.substr(pos+1);
                    if (k == "attempt_name") attempt_name = v;
                    else if (k == "success_name") success_name = v;
                    else if (k == "save_table") save_table = v;
                }
            }
        } catch(...) {}
    }

    // ENV override
    if (const char* en = std::getenv("ENIQ_QPI_ATTEMPT_NAME")) attempt_name = en;
    if (const char* en2 = std::getenv("ENIQ_QPI_SUCCESS_NAME")) success_name = en2;
    if (const char* st = std::getenv("ENIQ_QPI_SAVE_TABLE")) save_table = st;

    // Index helpers and optional CLI-driven index management
    auto create_indexes = [&](bool for_qpi)->void {
        char* ierr = nullptr;
        int rc = sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_pm_mo_ldn ON pm_counters(mo_ldn);", nullptr, nullptr, &ierr);
        if (rc != SQLITE_OK) { std::cerr << "create idx_pm_mo_ldn failed: " << (ierr?ierr:"") << "\n"; if (ierr) sqlite3_free(ierr); ierr = nullptr; }
        rc = sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_pm_timestamp ON pm_counters(timestamp);", nullptr, nullptr, &ierr);
        if (rc != SQLITE_OK) { std::cerr << "create idx_pm_timestamp failed: " << (ierr?ierr:"") << "\n"; if (ierr) sqlite3_free(ierr); ierr = nullptr; }
        rc = sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_pm_counter_name ON pm_counters(counter_name);", nullptr, nullptr, &ierr);
        if (rc != SQLITE_OK) { std::cerr << "create idx_pm_counter_name failed: " << (ierr?ierr:"") << "\n"; if (ierr) sqlite3_free(ierr); ierr = nullptr; }
        if (for_qpi && save_to_db) {
            // Ensure the save_table exists so creating an index won't fail
            std::string create_save = "CREATE TABLE IF NOT EXISTS " + save_table + " (mo_ldn TEXT, bucket_start INTEGER, attempts REAL, success REAL, rrc_success_rate REAL, interval_minutes INTEGER);";
            rc = sqlite3_exec(db, create_save.c_str(), nullptr, nullptr, &ierr);
            if (rc != SQLITE_OK) { std::cerr << "create save_table failed: " << (ierr?ierr:"") << "\n"; if (ierr) sqlite3_free(ierr); ierr = nullptr; }
            std::string idx_sql = "CREATE INDEX IF NOT EXISTS idx_qpi_mo_bucket ON " + save_table + "(mo_ldn, bucket_start);";
            rc = sqlite3_exec(db, idx_sql.c_str(), nullptr, nullptr, &ierr);
            if (rc != SQLITE_OK) { std::cerr << "create idx_qpi_mo_bucket failed: " << (ierr?ierr:"") << "\n"; if (ierr) sqlite3_free(ierr); ierr = nullptr; }
        }
    };

    auto drop_indexes = [&]()->void {
        char* ierr = nullptr;
        sqlite3_exec(db, "DROP INDEX IF EXISTS idx_pm_mo_ldn;", nullptr, nullptr, &ierr);
        if (ierr) { sqlite3_free(ierr); ierr = nullptr; }
        sqlite3_exec(db, "DROP INDEX IF EXISTS idx_pm_timestamp;", nullptr, nullptr, &ierr);
        if (ierr) { sqlite3_free(ierr); ierr = nullptr; }
        sqlite3_exec(db, "DROP INDEX IF EXISTS idx_pm_counter_name;", nullptr, nullptr, &ierr);
        if (ierr) { sqlite3_free(ierr); ierr = nullptr; }
        // drop qpi index (name is constant)
        sqlite3_exec(db, "DROP INDEX IF EXISTS idx_qpi_mo_bucket;", nullptr, nullptr, &ierr);
        if (ierr) { sqlite3_free(ierr); ierr = nullptr; }
    };

    // If user requested index operations, perform and exit
    if (create_indexes_flag) {
        create_indexes(true);
        std::cout << "Created indexes (best-effort).\n";
        sqlite3_close(db);
        return 0;
    }
    if (drop_indexes_flag) {
        drop_indexes();
        std::cout << "Dropped indexes (best-effort).\n";
        sqlite3_close(db);
        return 0;
    }
    if (rebuild_indexes_flag) {
        drop_indexes();
        create_indexes(true);
        std::cout << "Rebuilt indexes (best-effort).\n";
        sqlite3_close(db);
        return 0;
    }

    // If running QPI normally, create helpful indexes by default (best-effort)
    if (do_qpi) create_indexes(true);

    if (!do_qpi) {
        // default: print a quick row count
        sqlite3_stmt* stmt = nullptr;
        const char* qcount = "SELECT COUNT(*) FROM pm_counters;";
        if (sqlite3_prepare_v2(db, qcount, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                int cnt = sqlite3_column_int(stmt, 0);
                std::cout << "rows_count: " << cnt << "\n";
            }
            sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
        return 0;
    }

    // Build SQL for QPI aggregation
    std::string sql;
    if (interval_minutes > 0) {
        // group by interval bucket (unix epoch / (interval*60))
        sql = "SELECT mo_ldn, (CAST(strftime('%s', timestamp) AS INTEGER) / " + std::to_string(interval_minutes*60) + ") AS bucket, "
              "SUM(CASE WHEN counter_name = ? THEN value ELSE 0 END) AS attempts, "
              "SUM(CASE WHEN counter_name = ? THEN value ELSE 0 END) AS success "
              "FROM pm_counters ";
    } else {
        sql = "SELECT mo_ldn, "
              "SUM(CASE WHEN counter_name = ? THEN value ELSE 0 END) AS attempts, "
              "SUM(CASE WHEN counter_name = ? THEN value ELSE 0 END) AS success "
              "FROM pm_counters ";
    }

    // Add time filtering
    if (!start_ts.empty() && !end_ts.empty()) {
        sql += "WHERE timestamp BETWEEN ? AND ? ";
    } else if (!start_ts.empty()) {
        sql += "WHERE timestamp >= ? ";
    } else if (!end_ts.empty()) {
        sql += "WHERE timestamp <= ? ";
    }

    if (interval_minutes > 0) sql += "GROUP BY mo_ldn, bucket ORDER BY mo_ldn, bucket;";
    else sql += "GROUP BY mo_ldn ORDER BY mo_ldn;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare QPI query: " << sqlite3_errmsg(db) << "\n";
        sqlite3_close(db);
        return 1;
    }

    int bind_idx = 1;
    // bind attempt and success names
    sqlite3_bind_text(stmt, bind_idx++, attempt_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, bind_idx++, success_name.c_str(), -1, SQLITE_STATIC);

    // bind times if present
    if (!start_ts.empty() && !end_ts.empty()) {
        sqlite3_bind_text(stmt, bind_idx++, start_ts.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, bind_idx++, end_ts.c_str(), -1, SQLITE_STATIC);
    } else if (!start_ts.empty()) {
        sqlite3_bind_text(stmt, bind_idx++, start_ts.c_str(), -1, SQLITE_STATIC);
    } else if (!end_ts.empty()) {
        sqlite3_bind_text(stmt, bind_idx++, end_ts.c_str(), -1, SQLITE_STATIC);
    }

    // Prepare CSV output if requested
    std::ofstream ofs;
    bool write_csv = false;
    if (!out_csv.empty()) {
        ofs.open(out_csv, std::ios::out | std::ios::trunc);
        if (!ofs) {
            std::cerr << "Failed to open output CSV: " << out_csv << "\n";
            // continue printing to stdout instead
        } else {
            ofs << "mo_ldn";
            if (interval_minutes > 0) ofs << ",bucket_start";
            ofs << ",attempts,success,rrc_success_rate\n";
            write_csv = true;
        }
    }

    // Prepare DB insert if requested (save_to_db writes into the same DB `dbpath`)
    sqlite3_stmt* insert_stmt = nullptr;
    if (save_to_db) {
        // Create table if not exists
        std::string create_sql = "CREATE TABLE IF NOT EXISTS " + save_table + " (mo_ldn TEXT, bucket_start INTEGER, attempts REAL, success REAL, rrc_success_rate REAL, interval_minutes INTEGER);";
        char* err = nullptr;
        if (sqlite3_exec(db, create_sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            std::cerr << "Failed to create save table: " << (err?err:"") << "\n";
            if (err) sqlite3_free(err);
            // continue without failing
        }
        // prepare insert
        std::string insert_sql = "INSERT INTO " + save_table + " (mo_ldn, bucket_start, attempts, success, rrc_success_rate, interval_minutes) VALUES (?, ?, ?, ?, ?, ?);";
        if (sqlite3_prepare_v2(db, insert_sql.c_str(), -1, &insert_stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Failed to prepare insert statement: " << sqlite3_errmsg(db) << "\n";
            insert_stmt = nullptr;
        } else {
            // start a transaction for faster inserts and to ensure visibility
            char* terr = nullptr;
            sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, &terr);
            if (terr) { sqlite3_free(terr); terr = nullptr; }
        }
    }

    // Execute and print
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* mo = sqlite3_column_text(stmt, 0);
        long long bucket = 0;
        int col_offset = 1;
        if (interval_minutes > 0) {
            bucket = sqlite3_column_int64(stmt, 1);
            col_offset = 2;
        }
        double attempts = sqlite3_column_double(stmt, col_offset);
        double success = sqlite3_column_double(stmt, col_offset+1);
        double rate = -1.0;
        if (attempts > 0.0) rate = 100.0 * success / attempts;

        std::string mo_s = mo ? (const char*)mo : "(null)";
        if (write_csv) {
            ofs << '"' << mo_s << '"';
            if (interval_minutes > 0) ofs << "," << (bucket * (interval_minutes*60));
            ofs << "," << attempts << "," << success << ",";
            if (rate >= 0.0) ofs << rate; else ofs << "";
            ofs << "\n";
        } else {
            std::cout << mo_s;
            if (interval_minutes > 0) std::cout << "," << (bucket * (interval_minutes*60));
            std::cout << ", attempts=" << attempts << ", success=" << success;
            if (rate >= 0.0) std::cout << ", rate=" << rate;
            std::cout << "\n";
        }

        // Save into DB table if requested
        if (insert_stmt) {
            sqlite3_reset(insert_stmt);
            sqlite3_clear_bindings(insert_stmt);
            sqlite3_bind_text(insert_stmt, 1, mo_s.c_str(), -1, SQLITE_TRANSIENT);
            if (interval_minutes > 0) sqlite3_bind_int64(insert_stmt, 2, bucket * (interval_minutes*60));
            else sqlite3_bind_int64(insert_stmt, 2, 0);
            sqlite3_bind_double(insert_stmt, 3, attempts);
            sqlite3_bind_double(insert_stmt, 4, success);
            if (rate >= 0.0) sqlite3_bind_double(insert_stmt, 5, rate); else sqlite3_bind_null(insert_stmt, 5);
            sqlite3_bind_int(insert_stmt, 6, interval_minutes);
            if (sqlite3_step(insert_stmt) != SQLITE_DONE) {
                std::cerr << "Failed to insert QPI row: " << sqlite3_errmsg(db) << "\n";
            }
        }
    }

    if (write_csv) ofs.close();
    // finalize insert_stmt and commit
    if (insert_stmt) {
        sqlite3_finalize(insert_stmt);
        char* terr = nullptr;
        sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &terr);
        if (terr) { sqlite3_free(terr); terr = nullptr; }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
}
