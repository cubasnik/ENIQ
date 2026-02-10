#include <iostream>
#include <string>
#include <vector>
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
    // Build command to run the parser (assumes built in build\Release)
    wchar_t exePath[MAX_PATH];
    if (!GetFullPathNameW(L"build\\Release\\eniq_parser.exe", MAX_PATH, exePath, nullptr)) return 2;

    std::string out;
    bool ok = run_and_capture(std::wstring(exePath) + L" --lang=en", out);
    if (!ok) { std::cerr << "failed to run eniq_parser.exe --lang=en\n"; return 3; }
    if (out.find("Usage:") == std::string::npos) { std::cerr << "english output missing\n"; return 4; }

    out.clear();
    ok = run_and_capture(std::wstring(exePath) + L" --lang=ru", out);
    if (!ok) { std::cerr << "failed to run eniq_parser.exe --lang=ru\n"; return 5; }
    // Expect some non-ASCII bytes (UTF-8 Cyrillic) or Russian word
    bool has_non_ascii = false;
    for (unsigned char c : out) if (c >= 0x80) { has_non_ascii = true; break; }
    if (!has_non_ascii) { std::cerr << "russian output appears ASCII-only\n"; return 6; }

    std::cout << "localization tests OK\n";
    return 0;
}
