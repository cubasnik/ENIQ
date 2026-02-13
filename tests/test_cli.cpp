#include <iostream>
#include <string>
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
    wchar_t exePath[MAX_PATH];
    if (!GetFullPathNameW(L"build\\Release\\eniq_parser.exe", MAX_PATH, exePath, nullptr)) return 2;

    std::string out;
    bool ok = run_and_capture(std::wstring(exePath) + L" --help", out);
    if (!ok) { std::cerr << "failed to run --help\n"; return 3; }
    if (out.find("Usage:") == std::string::npos) { std::cerr << "help output missing\n"; return 4; }

    // Run with --db and no path -> should print usage and not create DB file
    std::wstring dbPath = L"eniq_cli_test.db";
    std::wstring cmd = std::wstring(exePath) + L" --db=" + dbPath;
    out.clear();
    ok = run_and_capture(cmd, out);
    if (!ok) { std::cerr << "failed to run --db\n"; return 5; }
    // ensure DB file not created because no path provided
    if (GetFileAttributesW(dbPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        // cleanup
        DeleteFileW(dbPath.c_str());
        std::cerr << "db file unexpectedly created\n"; return 6;
    }

    std::cout << "OK\n";
    return 0;
}
