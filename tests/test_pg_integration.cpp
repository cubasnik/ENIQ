#include <iostream>
#include <cstdlib>
#ifdef __has_include
# if __has_include(<pqxx/pqxx>)
#  include <pqxx/pqxx>
# endif
#endif

int main() {
#ifdef PQXX_VERSION_MAJOR
    const char* dsn = std::getenv("PGTEST_DSN");
    if (!dsn || !*dsn) {
        std::cout << "PGTEST_DSN not set; skipping PostgreSQL integration test\n";
        return 0; // skip
    }
    try {
        pqxx::connection conn(dsn);
        if (!conn.is_open()) {
            std::cerr << "Failed to open Postgres connection\n";
            return 1;
        }
        pqxx::work w(conn);
        // Use a temporary table so test is isolated
        w.exec("CREATE TEMP TABLE pm_counters_pg (id SERIAL PRIMARY KEY, timestamp TEXT, mo_ldn TEXT, meas_type TEXT, counter_name TEXT, value DOUBLE PRECISION);");
        w.exec_params("INSERT INTO pm_counters_pg (timestamp, mo_ldn, meas_type, counter_name, value) VALUES ($1, $2, $3, $4, $5);",
                      std::string("2026-02-10T00:00:00Z"), std::string("MO1"), std::string("typeA"), std::string("ctr1"), 123.45);
        w.exec_params("INSERT INTO pm_counters_pg (timestamp, mo_ldn, meas_type, counter_name, value) VALUES ($1, $2, $3, $4, $5);",
                      std::string("2026-02-10T00:01:00Z"), std::string("MO2"), std::string("typeB"), std::string("ctr2"), 67.89);
        w.commit();

        pqxx::work r(conn);
        pqxx::result res = r.exec("SELECT count(*) FROM pm_counters_pg;");
        if (res.size() != 1) {
            std::cerr << "Unexpected result size\n";
            return 1;
        }
        int cnt = res[0][0].as<int>();
        if (cnt != 2) {
            std::cerr << "Expected 2 rows, got " << cnt << "\n";
            return 1;
        }
        std::cout << "Postgres integration test OK\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Postgres test exception: " << e.what() << "\n";
        return 1;
    }
#else
    std::cout << "libpqxx not available at compile time; skipping test\n";
    return 0;
#endif
}
