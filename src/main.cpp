#include "xml_parser.h"
#include "db_writer.h"
#include <iostream>
#include <filesystem>
#include <vector>
#include <locale>
#include <clocale>

#include <string>
#include <algorithm>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <tlhelp32.h>
#endif

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
// On Windows we'll convert text to the console code page when printing.
    std::setlocale(LC_ALL, "");
    std::locale::global(std::locale());
    
    // Parse command-line: support --lang=ru|en (or --lang ru) and path
    std::string lang_arg;
    std::string path;
    bool force_cp65001 = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--lang=", 0) == 0) {
            lang_arg = a.substr(7);
        } else if (a == "--lang" && i + 1 < argc) {
            lang_arg = argv[++i];
        } else if (a == "--force-cp65001") {
            force_cp65001 = true;
        } else if (a.size() > 0 && a[0] == '-') {
            // ignore other options
        } else if (path.empty()) {
            path = a;
        }
    }
    
    enum class Lang { RU, EN } lang = Lang::EN;
    
    auto detect_ru = [&]() -> bool {
        try {
            std::string loc = std::locale("").name();
            std::transform(loc.begin(), loc.end(), loc.begin(), ::tolower);
            if (loc.find("ru") != std::string::npos) return true;
            if (loc.find("utf") != std::string::npos) return true;
        } catch (...) {}
        if (const char* env = std::getenv("LANG")) {
            std::string le = env;
            std::transform(le.begin(), le.end(), le.begin(), ::tolower);
            if (le.find("ru") != std::string::npos) return true;
            if (le.find("utf") != std::string::npos) return true;
        }
#ifdef _WIN32
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &mode)) {
            if (GetConsoleOutputCP() == 65001) return true;
        }
        LCID lcid = GetUserDefaultLCID();
        WORD prim = PRIMARYLANGID(LANGIDFROMLCID(lcid));
        if (prim == LANG_RUSSIAN) return true;
#endif
        return false;
    };
    
    if (!lang_arg.empty()) {
        std::string la = lang_arg;
        std::transform(la.begin(), la.end(), la.begin(), ::tolower);
        if (la == "ru" || la == "rus") lang = Lang::RU;
        else lang = Lang::EN;
    } else {
        if (detect_ru()) lang = Lang::RU;
        else lang = Lang::EN;
    }
    // Set global flag for db_writer language
    g_use_russian = (lang == Lang::RU);

    // If requested, try to set Windows console codepage to UTF-8 when RU selected
    // prefer forcing CP when requested or when running under PowerShell
    bool do_force_cp = false;
#ifdef _WIN32
    // detect parent process name (powershell/pwsh)
    auto parent_is_powershell = [&]() -> bool {
        DWORD pid = GetCurrentProcessId();
        DWORD ppid = 0;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return false;
        PROCESSENTRY32 pe = {};
        pe.dwSize = sizeof(pe);
        if (Process32First(snap, &pe)) {
            do {
                if (pe.th32ProcessID == pid) { ppid = pe.th32ParentProcessID; break; }
            } while (Process32Next(snap, &pe));
        }
        if (ppid == 0) { CloseHandle(snap); return false; }
        // find parent exe
        Process32First(snap, &pe);
        do {
            if (pe.th32ProcessID == ppid) {
                std::string name = pe.szExeFile;
                for (auto &c : name) c = (char)tolower(c);
                CloseHandle(snap);
                if (name.find("powershell.exe") != std::string::npos) return true;
                if (name.find("pwsh.exe") != std::string::npos) return true;
                return false;
            }
        } while (Process32Next(snap, &pe));
        CloseHandle(snap);
        return false;
    };

    if (lang == Lang::RU && (force_cp65001 || parent_is_powershell())) {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        do_force_cp = true;
    }
#endif
    
    if (path.empty()) {
        // Always print an ASCII English fallback so usage is readable in any shell
        std::cout << "Usage: eniq <path_to_xml_or_folder>\n";
        
        if (lang == Lang::RU) {
#ifdef _WIN32
            // Write Russian usage using console CP when possible, otherwise UTF-8
            const wchar_t wmsg[] = L"Использование: eniq <путь_к_xml_или_папке>\n";
            HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD written = 0;
            DWORD mode = 0;
            if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &mode)) {
                UINT cp = GetConsoleOutputCP();
                int needed = WideCharToMultiByte((UINT)cp, 0, wmsg, (int)wcslen(wmsg), nullptr, 0, nullptr, nullptr);
                if (needed > 0) {
                    std::string out;
                    out.resize(needed);
                    WideCharToMultiByte((UINT)cp, 0, wmsg, (int)wcslen(wmsg), &out[0], needed, nullptr, nullptr);
                    WriteFile(hOut, out.data(), (DWORD)out.size(), &written, nullptr);
                }
            } else {
                int needed = WideCharToMultiByte(CP_UTF8, 0, wmsg, (int)wcslen(wmsg), nullptr, 0, nullptr, nullptr);
                if (needed > 0) {
                    std::string out;
                    out.resize(needed);
                    WideCharToMultiByte(CP_UTF8, 0, wmsg, (int)wcslen(wmsg), &out[0], needed, nullptr, nullptr);
                    fwrite(out.data(), 1, out.size(), stdout);
                }
            }
#else
            std::cout << "Использование: eniq <путь_к_xml_или_папке>\n";
#endif
        }
        
        return 1;
    }

    // `path` is parsed above from arguments
    std::vector<CounterRecord> all;

    const std::string db = "eniq_data.db";

    if (!initDatabase(db)) return 1;

    if (fs::is_directory(path)) {
        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.is_regular_file() && entry.path().extension() == ".xml") {
                if (lang == Lang::RU) {
#ifdef _WIN32
                    const std::wstring wmsg = std::wstring(L"Обработка: ") + entry.path().filename().wstring() + L"\n";
                    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
                    DWORD written = 0;
                    if (hOut != INVALID_HANDLE_VALUE) {
                        UINT cp = GetConsoleOutputCP();
                        int needed = WideCharToMultiByte((UINT)cp, 0, wmsg.c_str(), (int)wmsg.size(), nullptr, 0, nullptr, nullptr);
                        if (needed > 0) {
                            std::string out;
                            out.resize(needed);
                            WideCharToMultiByte((UINT)cp, 0, wmsg.c_str(), (int)wmsg.size(), &out[0], needed, nullptr, nullptr);
                            WriteFile(hOut, out.data(), (DWORD)out.size(), &written, nullptr);
                        }
                    }
#else
                    std::cout << "Обработка: " << entry.path().filename() << "\n";
#endif
                } else {
                    std::cout << "Processing: " << entry.path().filename() << "\n";
                }
                std::vector<CounterRecord> recs;
                if (parse_ericssonsoft_pm_xml(entry.path().string(), recs)) {
                    saveRecords(db, recs);
                    all.insert(all.end(), recs.begin(), recs.end());
                }
            }
        }
    } else if (fs::exists(path) && fs::path(path).extension() == ".xml") {
        std::vector<CounterRecord> recs;
        if (parse_ericssonsoft_pm_xml(path, recs)) {
            saveRecords(db, recs);
        }
    }

    if (lang == Lang::RU) {
#ifdef _WIN32
        const wchar_t wmsg_done[] = L"Готово. Данные в ";
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD written = 0;
        if (hOut != INVALID_HANDLE_VALUE) {
            UINT cp = GetConsoleOutputCP();
            int needed = WideCharToMultiByte((UINT)cp, 0, wmsg_done, (int)wcslen(wmsg_done), nullptr, 0, nullptr, nullptr);
            if (needed > 0) {
                std::string out;
                out.resize(needed);
                WideCharToMultiByte((UINT)cp, 0, wmsg_done, (int)wcslen(wmsg_done), &out[0], needed, nullptr, nullptr);
                WriteFile(hOut, out.data(), (DWORD)out.size(), &written, nullptr);
            }
            std::string dbs = db + "\n";
            WriteFile(hOut, dbs.c_str(), (DWORD)dbs.size(), &written, nullptr);
        }
#else
        std::cout << "Готово. Данные в " << db << "\n";
#endif
    } else {
        std::cout << "Done. Data in " << db << "\n";
    }
    return 0;
}