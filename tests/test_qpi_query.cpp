#include <iostream>
#include <string>
#include <windows.h>
#include <sqlite3.h>

static bool run_and_capture(const std::wstring& cmdline, std::string &out) {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    if (!SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0)) { CloseHandle(hRead); CloseHandle(hWrite); return false; }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.hStdError = hWrite;
    si.hStdOutput = hWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, (LPWSTR)cmdline.c_str(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);
    if (!ok) { CloseHandle(hRead); return false; }

    const int bufSize = 4096;
    char buffer[bufSize];
    DWORD read = 0;
    out.clear();
    while (ReadFile(hRead, buffer, bufSize, &read, nullptr) && read > 0) {
        out.append(buffer, buffer + read);
    }

    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

int wmain() {
    // create test DB (remove any previous file to ensure deterministic contents)
    const wchar_t* dbfile_w = L"qpi_test.db";
    DeleteFileW(dbfile_w);
    const char* dbfile = "qpi_test.db";
    sqlite3* db = nullptr;
    if (sqlite3_open(dbfile, &db) != SQLITE_OK) {
        std::cerr << "failed to create test DB\n"; return 2;
    }
    const char* create = "CREATE TABLE IF NOT EXISTS pm_counters (timestamp TEXT, mo_ldn TEXT, meas_type TEXT, counter_name TEXT, value REAL);";
    sqlite3_exec(db, create, nullptr, nullptr, nullptr);

    // insert sample data
    // MO1: 100 attempts, 90 success
    // MO2: 50 attempts, 25 success
    const char* ins1 = "INSERT INTO pm_counters VALUES('2020-01-01T00:00:00Z','MO1','mt','rrcConnAttempt',100);";
    const char* ins2 = "INSERT INTO pm_counters VALUES('2020-01-01T00:00:00Z','MO1','mt','rrcConnSuccess',90);";
    const char* ins3 = "INSERT INTO pm_counters VALUES('2020-01-01T00:00:00Z','MO2','mt','rrcConnAttempt',50);";
    const char* ins4 = "INSERT INTO pm_counters VALUES('2020-01-01T00:00:00Z','MO2','mt','rrcConnSuccess',25);";
    sqlite3_exec(db, ins1, nullptr, nullptr, nullptr);
    sqlite3_exec(db, ins2, nullptr, nullptr, nullptr);
    sqlite3_exec(db, ins3, nullptr, nullptr, nullptr);
    sqlite3_exec(db, ins4, nullptr, nullptr, nullptr);
    sqlite3_close(db);

    // find query_db exe next to this test exe
    wchar_t modpath[MAX_PATH];
    if (!GetModuleFileNameW(NULL, modpath, MAX_PATH)) return 2;
    std::wstring modstr(modpath);
    size_t pos = modstr.find_last_of(L"\\/");
    std::wstring dir = (pos == std::wstring::npos) ? L"." : modstr.substr(0, pos);
    std::wstring exePath = dir + L"\\query_db.exe";

    std::wstring outCsv = L"qpi_out.csv";
    // remove old
    DeleteFileW(outCsv.c_str());

    std::wstring cmd = exePath + L" --qpi --db=" + std::wstring(L"qpi_test.db") + L" --out-csv=" + outCsv + L" --save-to-db --save-table=qpi_results";
    std::string out;
    bool ok = run_and_capture(cmd, out);
    if (!ok) { std::cerr << "failed to run query_db\n"; return 3; }

    // check CSV
    if (GetFileAttributesW(outCsv.c_str()) == INVALID_FILE_ATTRIBUTES) { std::cerr << "qpi_out.csv missing\n"; return 4; }

    // open and check contents
    FILE* f = nullptr;
    errno_t er = _wfopen_s(&f, outCsv.c_str(), L"r");
    if (er || !f) { std::cerr << "failed to open qpi_out.csv\n"; return 5; }
    char buf[1024];
    bool ok1=false, ok2=false;
    // skip header
    fgets(buf, sizeof(buf), f);
    while (fgets(buf, sizeof(buf), f)) {
        std::string s(buf);
        if (s.find("MO1") != std::string::npos && s.find("90") != std::string::npos) ok1 = true;
        if (s.find("MO2") != std::string::npos && s.find("25") != std::string::npos) ok2 = true;
    }
    fclose(f);
    if (!ok1 || !ok2) { std::cerr << "unexpected csv contents\n"; return 6; }

    // Validate database table entries
    sqlite3* vdb = nullptr;
    if (sqlite3_open("qpi_test.db", &vdb) != SQLITE_OK) { std::cerr << "failed open qpi_test.db\n"; return 7; }
    sqlite3_stmt* st = nullptr;
    const char* q = "SELECT mo_ldn, attempts, success FROM qpi_results ORDER BY mo_ldn";
    if (sqlite3_prepare_v2(vdb, q, -1, &st, nullptr) != SQLITE_OK) { std::cerr << "failed prepare select\n"; sqlite3_close(vdb); return 8; }
    bool got1=false, got2=false;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char* mo = sqlite3_column_text(st,0);
        double attempts = sqlite3_column_double(st,1);
        double success = sqlite3_column_double(st,2);
        std::string mo_s = mo ? (const char*)mo : "";
        if (mo_s == "MO1" && attempts == 100.0 && success == 90.0) got1 = true;
        if (mo_s == "MO2" && attempts == 50.0 && success == 25.0) got2 = true;
    }
    sqlite3_finalize(st);
    sqlite3_close(vdb);
    if (!got1 || !got2) { std::cerr << "db contents unexpected\n"; return 9; }

    std::cout << "qpi test OK\n";
    return 0;
}
