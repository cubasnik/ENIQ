#include <iostream>
#include <string>
#include <fstream>
#include <iterator>
#include <windows.h>

// Reuse runner from other tests
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

    std::wstring outDir = L"..\\csv_test_suffix_out";
    CreateDirectoryW(outDir.c_str(), nullptr);
    std::wstring dbPath = L"eniq_test_suffix.db";
    DeleteFileW((outDir + L"\\csv_summary.json").c_str());
    DeleteFileW(dbPath.c_str());

    std::wstring cmd = exePath + L" \"..\\data\\test.xml\" --db=" + dbPath + L" --csv=" + outDir + L" --csv-suffix-on-conflict --csv-suffix-format=increment --summary-path=" + outDir;
    std::string out;
    bool ok = run_and_capture(cmd, out);
    if (!ok) { std::cerr << "failed to run parser\n"; return 3; }

    // Ensure JSON summary was written and validate contents
    if (GetFileAttributesW((outDir + L"\\csv_summary.json").c_str()) == INVALID_FILE_ATTRIBUTES) { std::cerr << "csv_summary.json missing\n"; return 4; }
    {
        std::string jfpath;
        jfpath.assign(outDir.begin(), outDir.end()); jfpath += "\\csv_summary.json";
        std::ifstream jf(jfpath);
        if (!jf) { std::cerr << "failed to open csv_summary.json\n"; return 5; }
        std::string txt((std::istreambuf_iterator<char>(jf)), std::istreambuf_iterator<char>());
        if (txt.find("\"files\"") == std::string::npos) { std::cerr << "csv_summary.json missing 'files'\n"; return 6; }
        if (txt.find("test.csv") == std::string::npos) { std::cerr << "csv_summary.json missing test.csv\n"; return 7; }
    }

    // Also validate csv summary CSV exists and has header
    if (GetFileAttributesW((outDir + L"\\csv_summary.csv").c_str()) == INVALID_FILE_ATTRIBUTES) { std::cerr << "csv_summary.csv missing\n"; return 8; }
    {
        std::string csppath; csppath.assign(outDir.begin(), outDir.end()); csppath += "\\csv_summary.csv";
        std::ifstream cs(csppath);
        if (!cs) { std::cerr << "failed to open csv_summary.csv\n"; return 9; }
        std::string header;
        if (!std::getline(cs, header)) { std::cerr << "csv_summary.csv empty\n"; return 10; }
        if (header.find("path") == std::string::npos || header.find("rows") == std::string::npos) { std::cerr << "csv_summary.csv header missing\n"; return 11; }
    }

    std::cout << "csv suffix option test OK\n";
    return 0;
}
