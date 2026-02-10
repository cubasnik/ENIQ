# Copilot instructions

## Project snapshot (discoverable)
- This repo currently contains only a C/C++-style `.gitignore` and no committed source tree.
- The user provided a working example `main.cpp` that parses EricssonSoft PM XML and inserts rows into PostgreSQL using `libpqxx` and `pugixml` (header-only). That example is not yet in the repo but is the authoritative reference for expected behavior.

## Purpose of this guidance
Make an AI coding agent immediately productive for adding C++ tooling, parser code, and DB integration matching the provided example.

## Build / Run (concrete, reproducible)
- Compiler: code targets **C++17** and uses `std::filesystem` and `std::chrono`.
- Example native (Linux/macOS) build command from the example header in `main.cpp`:

```sh
g++ -std=c++17 -O2 -Wall main.cpp -lpqxx -lpq -lpugixml -o eniq_parser
```

- Notes:
	- `libpqxx` and `libpq` must be installed on the machine and available to the linker.
	- `pugixml` can be used header-only (include `pugixml.hpp`) or by adding `pugixml.cpp` to the build.
	- On Windows, adjust the compiler/linker and library names accordingly (MSVC/MinGW + correct .lib/.dll or .a).

## Runtime / DB integration
- The example connects to PostgreSQL via a libpqxx connection string (hardcoded in example):

```text
dbname=eniq user=postgres password=yourpass host=localhost port=5432
```

- Table referenced by the code: `pm_counters` with columns used in INSERT: `timestamp`, `mo_ldn`, `meas_type`, `counter_name`, `value`.
- Example minimal schema to create for local testing:

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

## Discoverable code patterns & conventions (from example)
- Parsers expect EricssonSoft PM XML `measCollec -> measInfo -> measValue -> r` patterns.
- The code collects `measTypes` (space-separated names) and maps `r` elements by index to those names.
- Use `std::filesystem` for directory traversal and `.xml` extension checks.
- Use `pqxx::work` for batched inserts and `exec_params` for parameterized queries.

## Project-specific recommendations for adding code
- Put new C++ source under a `src/` folder; place small test harness/executable at `src/main.cpp`.
- Add a `Makefile` or `CMakeLists.txt` (preferred) that documents required libraries: `libpqxx`, `libpq`, and optionally `pugixml`.
- If adding `pugixml` to repo, either:
	- vendor `pugixml.hpp` only and use header-only mode, or
	- add `pugixml.cpp` into the build for a stable single-file compile.

## Testing and quick verification
- To smoke-test parsing and DB insertion locally:

1. Create the `pm_counters` table (use the SQL above).
2. Build the example binary with the command above (adjusting for platform and installed libs).
3. Run the parser against a directory of EricssonSoft PM XML files or a single file:

```sh
./eniq_parser /path/to/pm_xml_files
```

## What to avoid / known assumptions
- Do not assume a JSON API or web server; current focus is a CLI batch parser that writes to Postgres.
- The example uses text for the `timestamp` column — if you prefer timestamps, convert and normalize when modifying schema.

## References & next actions for the agent
- If you add code, include a `README.md` with build instructions and a `CMakeLists.txt` or `Makefile`.
- When unsure about runtime DB credentials, prefer environment variables or a `config.json` and do not hardcode secrets in source.

---

If any of these assumptions are incorrect (different target language, DB, or desired output schema), tell me the intended direction and I will update these instructions. Please review and point out anything missing or unclear.
