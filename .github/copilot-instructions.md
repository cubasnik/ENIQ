# Copilot instructions

## Summary
Concise guidance to make an AI coding agent productive in this C++ repository that parses EricssonSoft PM XML and writes metrics to PostgreSQL. Focus areas: build and test on Windows (CMake/MSVC), XML parsing patterns, DB integration (`libpqxx`), and repo conventions.

## Project snapshot (discoverable)
- Sources: `src/` contains the main components: `xml_parser.cpp/.h`, `db_writer.cpp/.h`, `logger.cpp/.h`, `main.cpp`, and `query_db.cpp`.
- Tests: `tests/` contains unit/CLI tests (several test_*.cpp files and a Visual Studio test project in `build/`).
- Build: `CMakeLists.txt` at repo root; generated Visual Studio solution and project files exist under `build/`.
- Extras: `external/pugixml/` and `external/sqlite3.*` are vendored; `data/` holds sample XML files.

## Big picture & data flow
- Input: EricssonSoft PM XML files (see `data/test.xml`, `data/test_ns.xml`).
- Parsing: `xml_parser` reads `measCollec -> measInfo -> measValue -> r` and maps `measTypes` (space-separated) to `r` elements by index.
- Processing: `main.cpp` orchestrates directory traversal and hands parsed records to `db_writer`.
- Storage: `db_writer` uses `libpqxx` (`pqxx::work` + `exec_params`) to insert into PostgreSQL table `pm_counters`.

## Key files to inspect
- `src/xml_parser.cpp` / `src/xml_parser.h` — XML traversal + mapping `measTypes` → `r` values.
- `src/db_writer.cpp` / `src/db_writer.h` — connection handling, batching with `pqxx::work`, parameterized `exec_params` inserts.
- `src/main.cpp` — CLI entry: accepts file/dir paths, uses `std::filesystem` to iterate `.xml` files.
- `tests/test_pg_integration.cpp` — example of integration test that exercises DB inserts.

## Build & run (Windows-first examples)
- Configure (Visual Studio generator, x64):

```powershell
mkdir build
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

- Or use Ninja/MinGW if available:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

- Run the parser (binary appears under `build/` Visual Studio layout, e.g. `build/Release/eniq_parser.exe` or use VS debugger):

```powershell
build\Release\eniq_parser.exe C:\path\to\xml\dir
```

## Tests & CI
- Unit and integration tests live in `tests/` and are wired into the generated solution. Run with CTest or the Visual Studio Test Explorer.
- CI workflows: inspect `.github/workflows/windows-ci.yml` and `pg-integration.yml` for matrix env and Postgres setup details.

## Dependencies & integration points
- Runtime: `libpqxx` (+ `libpq`), `pugixml` (vendored under `external/pugixml/`), optional `sqlite3` in `external/`.
- DB: code expects a `pm_counters` table (columns: `timestamp`, `mo_ldn`, `meas_type`, `counter_name`, `value`). Prefer using environment variables for connection strings; tests/CI show how Postgres is provisioned.

## Repo-specific conventions & patterns
- Place new C++ files under `src/`, add targets in `CMakeLists.txt`.
- Parsing pattern: parse `measTypes` into an indexable vector and map each `r` element by the same index (see `xml_parser.cpp`).
- DB pattern: use `pqxx::work` for transactional batch inserts and `exec_params` for parameterized queries (see `db_writer.cpp`).
- Tests: follow `tests/test_*.cpp` naming; test projects are included in the CMake-generated solution.

## Quick verification steps
1. Create `pm_counters` table (example SQL in project root or below):

```sql
CREATE TABLE pm_counters (
	id SERIAL PRIMARY KEY,
	timestamp TEXT,
	mo_ldn TEXT,
	meas_type TEXT,
	counter_name TEXT,
	value DOUBLE PRECISION
);
```

2. Build using CMake (see above).
3. Run the parser against `data/` and confirm inserts via `query_db` or direct SQL.

## What to avoid / known assumptions
- This is a CLI batch parser focused on writing to Postgres — not a web service.
- Do not hardcode DB credentials; prefer `ENIQ_DB_CONN` or a `config.json` for secrets.

---

If anything here is incorrect or you want this adapted for Linux/macOS builds, additional CI, or switching to packaged `pugixml`, tell me which target and I'll update the file.
