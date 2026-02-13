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
    const wchar_t* dbfile_w = L"index_test.db";
    DeleteFileW(dbfile_w);
    const char* dbfile = "index_test.db";

    sqlite3* db = nullptr;
    if (sqlite3_open(dbfile, &db) != SQLITE_OK) { std::cerr << "failed to create test DB\n"; return 2; }
    const char* create = "CREATE TABLE IF NOT EXISTS pm_counters (timestamp TEXT, mo_ldn TEXT, meas_type TEXT, counter_name TEXT, value REAL);";
    sqlite3_exec(db, create, nullptr, nullptr, nullptr);
    sqlite3_close(db);

    // find query_db exe next to this test exe
    wchar_t modpath[MAX_PATH];
    if (!GetModuleFileNameW(NULL, modpath, MAX_PATH)) return 2;
    std::wstring modstr(modpath);
    size_t pos = modstr.find_last_of(L"\\/");
    std::wstring dir = (pos == std::wstring::npos) ? L"." : modstr.substr(0, pos);
    std::wstring exePath = dir + L"\\query_db.exe";

    // Run create-indexes (with --save-to-db so qpi index is created too)
    std::wstring cmd = exePath + L" --create-indexes --db=" + std::wstring(L"index_test.db") + L" --save-to-db --save-table=qpi_results";
    std::string out;
    if (!run_and_capture(cmd, out)) { std::cerr << "failed to run query_db --create-indexes\n"; return 3; }

    // verify indexes exist
    sqlite3* vdb = nullptr;
    if (sqlite3_open(dbfile, &vdb) != SQLITE_OK) { std::cerr << "failed open index_test.db\n"; return 4; }
    sqlite3_stmt* st = nullptr;
    const char* q = "SELECT name FROM sqlite_master WHERE type='index' ORDER BY name";
    if (sqlite3_prepare_v2(vdb, q, -1, &st, nullptr) != SQLITE_OK) { std::cerr << "failed prepare select\n"; sqlite3_close(vdb); return 5; }
    bool found_pm=false, found_ts=false, found_cn=false, found_qpi=false;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(st,0);
        std::string n = name ? (const char*)name : "";
        if (n == "idx_pm_mo_ldn") found_pm = true;
        if (n == "idx_pm_timestamp") found_ts = true;
        if (n == "idx_pm_counter_name") found_cn = true;
        if (n == "idx_qpi_mo_bucket") found_qpi = true;
    }
    sqlite3_finalize(st);
    if (!found_pm || !found_ts || !found_cn || !found_qpi) { std::cerr << "indexes not created as expected\n"; sqlite3_close(vdb); return 6; }

    // Now run rebuild-indexes
    std::wstring cmd2 = exePath + L" --rebuild-indexes --db=" + std::wstring(L"index_test.db") + L" --save-to-db --save-table=qpi_results";
    out.clear();
    if (!run_and_capture(cmd2, out)) { std::cerr << "failed to run query_db --rebuild-indexes\n"; sqlite3_close(vdb); return 7; }

    // verify again
    sqlite3_stmt* st2 = nullptr;
    if (sqlite3_prepare_v2(vdb, q, -1, &st2, nullptr) != SQLITE_OK) { std::cerr << "failed prepare select2\n"; sqlite3_close(vdb); return 8; }
    bool f_pm2=false, f_ts2=false, f_cn2=false, f_qpi2=false;
    while (sqlite3_step(st2) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(st2,0);
        std::string n = name ? (const char*)name : "";
        if (n == "idx_pm_mo_ldn") f_pm2 = true;
        if (n == "idx_pm_timestamp") f_ts2 = true;
        if (n == "idx_pm_counter_name") f_cn2 = true;
        if (n == "idx_qpi_mo_bucket") f_qpi2 = true;
    }
    sqlite3_finalize(st2);
    sqlite3_close(vdb);
    if (!f_pm2 || !f_ts2 || !f_cn2 || !f_qpi2) { std::cerr << "indexes missing after rebuild\n"; return 9; }

    // now drop indexes
    std::wstring cmd3 = exePath + L" --drop-indexes --db=" + std::wstring(L"index_test.db");
    out.clear();
    if (!run_and_capture(cmd3, out)) { std::cerr << "failed to run query_db --drop-indexes\n"; return 10; }

    // verify none remain
    sqlite3* vdb2 = nullptr;
    if (sqlite3_open(dbfile, &vdb2) != SQLITE_OK) { std::cerr << "failed open index_test.db second\n"; return 11; }
    sqlite3_stmt* st3 = nullptr;
    if (sqlite3_prepare_v2(vdb2, q, -1, &st3, nullptr) != SQLITE_OK) { std::cerr << "failed prepare select3\n"; sqlite3_close(vdb2); return 12; }
    bool any=false;
    while (sqlite3_step(st3) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(st3,0);
        std::string n = name ? (const char*)name : "";
        if (n == "idx_pm_mo_ldn" || n == "idx_pm_timestamp" || n == "idx_pm_counter_name" || n == "idx_qpi_mo_bucket") any = true;
    }
    sqlite3_finalize(st3);
    sqlite3_close(vdb2);
    if (any) { std::cerr << "indexes still present after drop\n"; return 13; }

    std::cout << "rebuild-indexes test OK\n";
    return 0;
}
