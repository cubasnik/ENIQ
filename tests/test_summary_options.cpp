#include <iostream>
#include <string>
#include <fstream>
#include <iterator>
#include <windows.h>

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
    wchar_t modpath[MAX_PATH];
    if (!GetModuleFileNameW(NULL, modpath, MAX_PATH)) return 2;
    std::wstring modstr(modpath);
    size_t pos = modstr.find_last_of(L"\\/");
    std::wstring dir = (pos == std::wstring::npos) ? L"." : modstr.substr(0, pos);
    std::wstring exePath = dir + L"\\eniq_parser.exe";

    // prepare temporary output directory
    std::wstring outDir = L"..\\csv_test_summary_out";
    CreateDirectoryW(outDir.c_str(), nullptr);

    std::wstring dbPath = L"eniq_test_summary.db";
    // remove any previous files
    DeleteFileW((outDir + L"\\summary.json").c_str());
    DeleteFileW((outDir + L"\\summary.csv").c_str());
    DeleteFileW(dbPath.c_str());

    std::wstring cmd = exePath + L" \"..\\data\\test.xml\" --db=" + dbPath + L" --csv=" + outDir + L" --summary-path=" + outDir + L" --summary-filename=summary.json";
    std::string out;
    bool ok = run_and_capture(cmd, out);
    if (!ok) { std::cerr << "failed to run parser\n"; return 3; }

    // Check that summary.json and summary.csv exist
    if (GetFileAttributesW((outDir + L"\\summary.json").c_str()) == INVALID_FILE_ATTRIBUTES) { std::cerr << "summary.json missing\n"; return 4; }
    if (GetFileAttributesW((outDir + L"\\summary.csv").c_str()) == INVALID_FILE_ATTRIBUTES) { std::cerr << "summary.csv missing\n"; return 5; }

    // Validate JSON contents (contains "files", "total_rows" and test.csv)
    {
        std::string jfpath;
        jfpath.assign(outDir.begin(), outDir.end()); jfpath += "\\summary.json";
        std::ifstream jf(jfpath);
        if (!jf) { std::cerr << "failed to open summary.json\n"; return 6; }
        std::string txt((std::istreambuf_iterator<char>(jf)), std::istreambuf_iterator<char>());
        if (txt.find("\"files\"") == std::string::npos) { std::cerr << "summary.json missing 'files'\n"; return 7; }
        if (txt.find("\"total_rows\"") == std::string::npos) { std::cerr << "summary.json missing 'total_rows'\n"; return 8; }
        if (txt.find("test.csv") == std::string::npos) { std::cerr << "summary.json missing 'test.csv'\n"; return 9; }
    }

    // Validate CSV summary contents (header and test.csv line)
    {
        std::string csppath;
        csppath.assign(outDir.begin(), outDir.end()); csppath += "\\summary.csv";
        std::ifstream cs(csppath);
        if (!cs) { std::cerr << "failed to open summary.csv\n"; return 10; }
        std::string header;
        if (!std::getline(cs, header)) { std::cerr << "summary.csv empty\n"; return 11; }
        if (header.find("path") == std::string::npos || header.find("rows") == std::string::npos) { std::cerr << "summary.csv header missing\n"; return 12; }
        std::string line;
        bool found = false;
        while (std::getline(cs, line)) {
            if (line.find("test.csv") != std::string::npos) { found = true; break; }
        }
        if (!found) { std::cerr << "summary.csv missing test.csv entry\n"; return 13; }
    }

    std::cout << "summary options test OK\n";
    return 0;
}
