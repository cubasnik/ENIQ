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
#include "logger.h"
#include <fstream>
#include <map>
#include <cerrno>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
// On Windows we'll convert text to the console code page when printing.
    std::setlocale(LC_ALL, "");
    std::locale::global(std::locale());
    
    // Parse command-line: support --lang=ru|en (or --lang ru) and path
    std::string lang_arg;
    std::string path;
    std::string db_arg;
    int verbose_count = 0;
    bool force_cp65001 = false;
    bool recurse_flag = false;
    bool clean_db_flag = false;
    bool clean_test_flag = false;
    bool dry_run_flag = false;
    bool confirm_yes = false;
    std::string csv_path;
    bool csv_suffix_on_conflict = false;
    std::string summary_path;
    std::string summary_filename;
    std::string csv_suffix_format = "increment"; // options: "timestamp" or "increment"
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--lang=", 0) == 0) {
            lang_arg = a.substr(7);
        } else if (a == "--lang" && i + 1 < argc) {
            lang_arg = argv[++i];
        } else if (a == "--force-cp65001") {
            force_cp65001 = true;
        } else if (a.rfind("--db=", 0) == 0) {
            db_arg = a.substr(5);
        } else if (a == "--db" && i + 1 < argc) {
            db_arg = argv[++i];
        } else if (a == "-v" || a == "--verbose") {
            ++verbose_count;
        } else if (a == "--recursive") {
            recurse_flag = true;
        } else if (a == "--clean-db") {
            clean_db_flag = true;
        } else if (a == "--clean-test-records") {
            clean_test_flag = true;
        } else if (a == "--yes") {
            confirm_yes = true;
        } else if (a == "--dry-run" || a == "--clean-test-dry-run") {
            dry_run_flag = true;
        } else if (a.rfind("--csv=", 0) == 0) {
            csv_path = a.substr(6);
        } else if (a == "--csv" && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (a == "--csv-suffix-on-conflict") {
            csv_suffix_on_conflict = true;
        } else if (a.rfind("--summary-path=", 0) == 0) {
            summary_path = a.substr(15);
        } else if (a == "--summary-path" && i + 1 < argc) {
            summary_path = argv[++i];
        } else if (a.rfind("--summary-filename=", 0) == 0) {
            summary_filename = a.substr(19);
        } else if (a == "--summary-filename" && i + 1 < argc) {
            summary_filename = argv[++i];
        } else if (a.rfind("--csv-suffix-format=", 0) == 0) {
            csv_suffix_format = a.substr(20);
        } else if (a == "--csv-suffix-format" && i + 1 < argc) {
            csv_suffix_format = argv[++i];
        } else if (a == "--config" && i + 1 < argc) {
            // stub: future config support
            ++i;
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

    // Set logger verbosity
    if (verbose_count >= 2) logger::set_level(LogLevel::Debug);
    else if (verbose_count == 1) logger::set_level(LogLevel::Info);
    else logger::set_level(LogLevel::Warn);

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

    const std::string db = db_arg.empty() ? "eniq_data.db" : db_arg;
    // Optionally clean existing DB file before initializing
    if (clean_db_flag) {
        try {
            if (fs::exists(db)) {
                fs::remove(db);
                logger::info(std::string("Removed existing DB: ") + db);
            }
        } catch (...) {
            logger::warn(std::string("Failed to remove existing DB (continuing): ") + db);
        }
    }

    if (!initDatabase(db)) return 1;

    if (clean_test_flag) {
        std::vector<CounterRecord> candidates;
        if (!dryRunCleanTestRecords(db, candidates)) {
            logger::warn(std::string("Dry-run lookup failed for DB: ") + db);
            return 1;
        }

        if (dry_run_flag) {
            std::cout << "Dry-run: would remove " << candidates.size() << " records:\n";
            for (const auto &r : candidates) {
                std::cout << r.timestamp << " | " << r.mo_ldn << " | " << (r.meas_type.empty() ? "(empty)" : r.meas_type) << " | " << r.counter_name << " | " << r.value << "\n";
            }
            return 0;
        }

        if (candidates.empty()) {
            std::cout << "No test/unusual records found to remove.\n";
            return 0;
        }

        // If --yes provided, skip interactive prompt
        bool proceed = false;
        if (confirm_yes) proceed = true;
        else {
            std::cout << "About to remove " << candidates.size() << " records. Proceed? (y/N): ";
            std::string ans;
            if (!std::getline(std::cin, ans)) {
                std::cout << "Input error, aborting.\n";
                return 1;
            }
            if (!ans.empty() && (ans[0] == 'y' || ans[0] == 'Y')) proceed = true;
        }

        if (!proceed) {
            std::cout << "Aborted by user. No changes made.\n";
            return 0;
        }

        if (!cleanTestRecords(db)) {
            logger::warn(std::string("Failed to purge test records in DB: ") + db);
        }
    }

    // Helper: write CSV per input file when requested
    bool input_was_directory = fs::is_directory(path);
    std::map<std::string, std::size_t> csv_summary;
    auto write_csv_for = [&](const std::string &inPath, const std::vector<CounterRecord>& recs){
        if (csv_path.empty()) return;
        fs::path outBase(csv_path);
        fs::path inP(inPath);

        bool csv_dir = false;
        // treat as directory if csv_path exists and is directory, or ends with separator, or input was a directory
        if ((fs::exists(outBase) && fs::is_directory(outBase)) ) csv_dir = true;
        std::string s = csv_path;
        if (!s.empty()) {
            char last = s.back();
            if (last == '/' || last == '\\') csv_dir = true;
        }
        if (input_was_directory) csv_dir = true;

        fs::path outFile;
        if (csv_dir) {
            // ensure directory exists
            std::error_code ec;
            fs::create_directories(outBase, ec);
            std::string fname = inP.filename().string();
            // strip .xml.gz or .xml or .gz
            if (fname.size() > 7 && fname.substr(fname.size()-7) == ".xml.gz") fname = fname.substr(0, fname.size()-7);
            else if (fname.size() > 4 && fname.substr(fname.size()-4) == ".xml") fname = fname.substr(0, fname.size()-4);
            else if (fname.size() > 3 && fname.substr(fname.size()-3) == ".gz") fname = fname.substr(0, fname.size()-3);
            outFile = outBase / (fname + ".csv");
        } else {
            outFile = outBase;
        }

        bool need_header = true;
        if (fs::exists(outFile)) {
            try { if (fs::file_size(outFile) > 0) need_header = false; } catch(...) { }
        }

        // helper to clear read-only attribute (platform-specific)
        auto clear_readonly = [&](const fs::path &p)->bool {
#ifdef _WIN32
            std::wstring wp = p.wstring();
            DWORD attrs = GetFileAttributesW(wp.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES) return false;
            if (attrs & FILE_ATTRIBUTE_READONLY) {
                attrs &= ~FILE_ATTRIBUTE_READONLY;
                if (!SetFileAttributesW(wp.c_str(), attrs)) return false;
                return true;
            }
            return false;
#else
            std::error_code ec;
            fs::permissions(p, fs::perms::owner_write, fs::perm_options::add, ec);
            return !ec;
#endif
        };

        // Ensure parent directory exists
        fs::path parent = outFile.parent_path();
        if (!parent.empty() && !fs::exists(parent)) {
            std::error_code ec;
            fs::create_directories(parent, ec);
            if (ec) {
                logger::error(std::string("Failed to create CSV directory: ") + parent.string() + std::string(" : ") + ec.message());
                return;
            }
        }
        if (fs::exists(outFile) && fs::is_directory(outFile)) {
            logger::error(std::string("CSV target is a directory, not a file: ") + outFile.string());
            return;
        }

        std::ofstream ofs(outFile, std::ios::out | std::ios::app);
        if (!ofs) {
            int e = errno;
            std::string em = e ? std::strerror(e) : "(unknown)";
            logger::warn(std::string("Failed to open CSV for writing (first attempt): ") + outFile.string() + " errno=" + std::to_string(e) + " (" + em + ")");

            // Try to clear read-only attribute, then retry
            bool tried_fix = false;
            if (fs::exists(outFile)) {
                if (clear_readonly(outFile)) {
                    tried_fix = true;
                    logger::info(std::string("Cleared read-only attribute on: ") + outFile.string());
                }
            }

            if (tried_fix) {
                ofs.open(outFile, std::ios::out | std::ios::app);
            }
        }

        if (!ofs) {
            // If still cannot open, attempt write to temp file and move
            int e = errno;
            std::string em = e ? std::strerror(e) : "(unknown)";
            logger::warn(std::string("Opening CSV failed; attempting temp-write fallback: ") + outFile.string() + " errno=" + std::to_string(e) + " (" + em + ")");

            // Create unique temp filename in same directory
            std::string tmpname;
            {
                auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
                unsigned long pid = 0;
#ifdef _WIN32
                pid = GetCurrentProcessId();
#else
                pid = (unsigned long)getpid();
#endif
                tmpname = outFile.string() + ".tmp." + std::to_string(pid) + "." + std::to_string(now);
            }
            fs::path tmpPath(tmpname);

            std::ofstream tofs(tmpPath, std::ios::out | std::ios::trunc);
            if (!tofs) {
                int e2 = errno;
                std::string em2 = e2 ? std::strerror(e2) : "(unknown)";
                logger::error(std::string("Failed to open temporary CSV for writing: ") + tmpPath.string() + " errno=" + std::to_string(e2) + " (" + em2 + ")");
                return;
            }
            // write header if file new
            if (!fs::exists(outFile)) {
                tofs << "timestamp,mo_ldn,meas_type,counter_name,value\n";
            }
            std::size_t written = 0;
            auto escape2 = [&](const std::string &s)->std::string {
                std::string out;
                out.reserve(s.size()+2);
                out.push_back('"');
                for (char c : s) {
                    if (c == '"') out.append("\"\""); else out.push_back(c);
                }
                out.push_back('"');
                return out;
            };
            for (const auto &r : recs) {
                tofs << escape2(r.timestamp) << ',' << escape2(r.mo_ldn) << ',' << escape2(r.meas_type) << ',' << escape2(r.counter_name) << ',';
                tofs.setf(std::ios::fmtflags(0), std::ios::floatfield);
                tofs << r.value << '\n';
                ++written;
            }
            tofs.close();

            // Try to replace target
            std::error_code ec;
            // If target exists and is readonly, try clear
            if (fs::exists(outFile)) {
                if (!fs::remove(outFile, ec)) {
                    // try clearing readonly attr then remove
                    if (clear_readonly(outFile)) {
                        fs::remove(outFile, ec);
                    }
                }
                // rename tmp to target
                fs::rename(tmpPath, outFile, ec);
                if (ec) {
                    logger::warn(std::string("Rename failed (will try copy): ") + ec.message());
                    std::error_code ec2;
                    if (fs::copy_file(tmpPath, outFile, fs::copy_options::overwrite_existing, ec2)) {
                        fs::remove(tmpPath, ec2);
                    } else {
                        // If configured, fall back to writing with unique suffix instead of failing
                        if (csv_suffix_on_conflict) {
                            // generate conflict filename according to csv_suffix_format
                            std::string stem = outFile.filename().string();
                            std::string ext = outFile.extension().string();
                            std::string nameOnly = stem;
                            if (!ext.empty()) {
                                size_t pos = stem.rfind(ext);
                                if (pos != std::string::npos) nameOnly = stem.substr(0, pos);
                            }

                            std::error_code ec3;
                            bool wroteConflict = false;
                            if (csv_suffix_format == "increment") {
                                // try incremental suffixes until a free slot or until max attempts
                                const int MAX_TRIES = 10000;
                                for (int i = 1; i <= MAX_TRIES; ++i) {
                                    std::string conflictName = nameOnly + ".conflict." + std::to_string(i) + ext;
                                    fs::path conflictPath = outFile.parent_path() / conflictName;
                                    if (!fs::exists(conflictPath)) {
                                        // try rename first
                                        fs::rename(tmpPath, conflictPath, ec3);
                                        if (!ec3) {
                                            logger::info(std::string("Wrote CSV to conflict file: ") + conflictPath.string() + std::string(" (added ") + std::to_string(written) + " rows)");
                                            csv_summary[conflictPath.string()] += written;
                                            wroteConflict = true;
                                            break;
                                        }
                                        // try copy fallback
                                        if (fs::copy_file(tmpPath, conflictPath, fs::copy_options::overwrite_existing, ec3)) {
                                            fs::remove(tmpPath, ec3);
                                            logger::info(std::string("Wrote CSV to conflict file (copied): ") + conflictPath.string() + std::string(" (added ") + std::to_string(written) + " rows)");
                                            csv_summary[conflictPath.string()] += written;
                                            wroteConflict = true;
                                            break;
                                        }
                                    }
                                }
                                if (!wroteConflict) {
                                    logger::error(std::string("Failed to write conflict CSV after incremental attempts"));
                                    return;
                                }
                            } else {
                                // default: timestamp-based suffix
                                auto nown = std::chrono::system_clock::now().time_since_epoch().count();
                                std::string conflictName = nameOnly + ".conflict." + std::to_string(nown) + ext;
                                fs::path conflictPath = outFile.parent_path() / conflictName;
                                fs::rename(tmpPath, conflictPath, ec3);
                                if (!ec3) {
                                    logger::info(std::string("Wrote CSV to conflict file: ") + conflictPath.string() + std::string(" (added ") + std::to_string(written) + " rows)");
                                    csv_summary[conflictPath.string()] += written;
                                    return;
                                } else {
                                    // try copy to conflict
                                    if (fs::copy_file(tmpPath, conflictPath, fs::copy_options::overwrite_existing, ec3)) {
                                        fs::remove(tmpPath, ec3);
                                        logger::info(std::string("Wrote CSV to conflict file (copied): ") + conflictPath.string() + std::string(" (added ") + std::to_string(written) + " rows)");
                                        csv_summary[conflictPath.string()] += written;
                                        return;
                                    } else {
                                        logger::error(std::string("Failed to copy temp CSV to target and conflict file: ") + ec3.message());
                                        return;
                                    }
                                }
                            }
                        }
                        logger::error(std::string("Failed to copy temp CSV to target: ") + ec2.message());
                        // leave temp file for inspection
                        return;
                    }
                }
                logger::info(std::string("Wrote CSV via temp file: ") + outFile.string() + std::string(" (added ") + std::to_string(written) + " rows)");
                csv_summary[outFile.string()] += written;
                return;
            }
        }
        ofs.imbue(std::locale::classic());
        auto escape = [&](const std::string &s)->std::string {
            std::string out;
            out.reserve(s.size()+2);
            out.push_back('"');
            for (char c : s) {
                if (c == '"') out.append("\"\""); else out.push_back(c);
            }
            out.push_back('"');
            return out;
        };
        if (need_header) ofs << "timestamp,mo_ldn,meas_type,counter_name,value\n";
        std::size_t written = 0;
        for (const auto &r : recs) {
            ofs << escape(r.timestamp) << ',' << escape(r.mo_ldn) << ',' << escape(r.meas_type) << ',' << escape(r.counter_name) << ',';
            ofs.setf(std::ios::fmtflags(0), std::ios::floatfield);
            ofs << r.value << '\n';
            ++written;
        }
        ofs.close();
        logger::info(std::string("Wrote CSV: ") + outFile.string() + std::string(" (added ") + std::to_string(written) + " rows)");
        csv_summary[outFile.string()] += written;
    };

    if (dry_run_flag) {
        std::vector<CounterRecord> candidates;
        if (!dryRunCleanTestRecords(db, candidates)) {
            logger::warn(std::string("Dry-run lookup failed for DB: ") + db);
            return 1;
        }
        std::cout << "Dry-run: would remove " << candidates.size() << " records:\n";
        for (const auto &r : candidates) {
            std::cout << r.timestamp << " | " << r.mo_ldn << " | " << (r.meas_type.empty() ? "(empty)" : r.meas_type) << " | " << r.counter_name << " | " << r.value << "\n";
        }
        return 0;
    }

    if (fs::is_directory(path)) {
        if (recurse_flag) {
            for (const auto& entry : fs::recursive_directory_iterator(path)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                bool isXml = (ext == ".xml");
                bool isGz = (ext == ".gz") || (entry.path().filename().string().size() > 7 &&
                             entry.path().filename().string().substr(entry.path().filename().string().size()-7) == ".xml.gz");
                if (!(isXml || isGz)) continue;
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
                    logger::info(std::string("Обработка: ") + entry.path().filename().string());
#endif
                } else {
                    std::cout << "Processing: " << entry.path().filename() << "\n";
                    logger::info(std::string("Processing: ") + entry.path().filename().string());
                }
                std::vector<CounterRecord> recs;
                if (parse_ericssonsoft_pm_xml(entry.path().string(), recs)) {
                    saveRecords(db, recs);
                    all.insert(all.end(), recs.begin(), recs.end());
                    write_csv_for(entry.path().string(), recs);
                }
            }
        } else {
            for (const auto& entry : fs::directory_iterator(path)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                bool isXml = (ext == ".xml");
                bool isGz = (ext == ".gz") || (entry.path().filename().string().size() > 7 &&
                             entry.path().filename().string().substr(entry.path().filename().string().size()-7) == ".xml.gz");
                if (!(isXml || isGz)) continue;
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
                    logger::info(std::string("Обработка: ") + entry.path().filename().string());
#endif
                } else {
                    std::cout << "Processing: " << entry.path().filename() << "\n";
                    logger::info(std::string("Processing: ") + entry.path().filename().string());
                }
                std::vector<CounterRecord> recs;
                if (parse_ericssonsoft_pm_xml(entry.path().string(), recs)) {
                    saveRecords(db, recs);
                    all.insert(all.end(), recs.begin(), recs.end());
                    write_csv_for(entry.path().string(), recs);
                }
            }
        }
    } else if (fs::exists(path) && (fs::path(path).extension() == ".xml" || fs::path(path).extension() == ".gz")) {
        std::vector<CounterRecord> recs;
        if (parse_ericssonsoft_pm_xml(path, recs)) {
            saveRecords(db, recs);
            all.insert(all.end(), recs.begin(), recs.end());
            write_csv_for(path, recs);
        }
    }

    // per-file CSV writing handled inline via write_csv_for

    // Summary report for CSVs: table and JSON
    if (!csv_summary.empty()) {
        std::size_t total = 0;
        for (const auto &p : csv_summary) total += p.second;

        // Console table
        size_t maxpath = 4; // min width for 'Path'
        for (const auto &p : csv_summary) if (p.first.size() > maxpath) maxpath = p.first.size();
        std::string hdr = "Path";
        std::cout << std::left << std::setw((int)maxpath+2) << hdr << "Rows\n";
        std::cout << std::string(maxpath+2, '-') << "----\n";
        for (const auto &p : csv_summary) {
            std::cout << std::left << std::setw((int)maxpath+2) << p.first << p.second << "\n";
        }
        std::cout << std::string(maxpath+2, '-') << "----\n";
        std::cout << std::left << std::setw((int)maxpath+2) << "Total" << total << "\n";
        logger::info(std::string("CSV summary: wrote total ") + std::to_string(total) + " rows to " + std::to_string(csv_summary.size()) + " files");

        // JSON file (honor explicit --summary-path and --summary-filename)
        fs::path jsonOut;
        auto ensure_json_name = [&](const fs::path &p)->fs::path {
            if (p.has_extension()) return p;
            return p / "csv_summary.json";
        };

        if (!summary_path.empty()) {
            fs::path sp(summary_path);
            if (!summary_filename.empty()) {
                fs::path fn(summary_filename);
                if (!fn.has_extension()) fn += ".json";
                if (fs::exists(sp) && fs::is_directory(sp)) jsonOut = sp / fn;
                else {
                    // summary_path treated as directory if ends with separator
                    std::string s = summary_path;
                    if (!s.empty() && (s.back() == '/' || s.back() == '\\')) jsonOut = fs::path(s) / fn;
                    else jsonOut = sp.parent_path() / fn;
                }
            } else {
                if (fs::exists(sp) && fs::is_directory(sp)) jsonOut = sp / "csv_summary.json";
                else {
                    std::string s = summary_path;
                    if (!s.empty() && (s.back() == '/' || s.back() == '\\')) jsonOut = fs::path(s) / "csv_summary.json";
                    else jsonOut = sp.parent_path() / "csv_summary.json";
                }
            }
        } else if (!summary_filename.empty()) {
            fs::path fn(summary_filename);
            if (!fn.has_extension()) fn += ".json";
            if (!csv_path.empty()) {
                fs::path cp(csv_path);
                if (fs::exists(cp) && fs::is_directory(cp)) jsonOut = cp / fn;
                else jsonOut = cp.parent_path() / fn;
            } else {
                jsonOut = fs::current_path() / fn;
            }
        } else if (!csv_path.empty()) {
            fs::path cp(csv_path);
            if (fs::exists(cp) && fs::is_directory(cp)) jsonOut = cp / "csv_summary.json";
            else {
                std::string s = csv_path;
                if (!s.empty() && (s.back() == '/' || s.back() == '\\')) jsonOut = fs::path(s) / "csv_summary.json";
                else jsonOut = cp.parent_path() / "csv_summary.json";
            }
        } else {
            jsonOut = fs::path("csv_summary.json");
        }

        auto json_escape = [](const std::string &s)->std::string {
            std::string out;
            out.reserve(s.size() + 8);
            for (unsigned char c : s) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\b': out += "\\b"; break;
                    case '\f': out += "\\f"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (c < 0x20) {
                            const char *hex = "0123456789abcdef";
                            out += "\\u00";
                            out += hex[(c >> 4) & 0xF];
                            out += hex[c & 0xF];
                        } else {
                            out.push_back((char)c);
                        }
                }
            }
            return out;
        };

        std::ofstream jf(jsonOut, std::ios::out | std::ios::trunc);
        if (!jf) {
            logger::error(std::string("Failed to open JSON summary for writing: ") + jsonOut.string());
        } else {
            jf << "{\n";
            jf << "  \"files\": [\n";
            bool first = true;
            for (const auto &p : csv_summary) {
                if (!first) jf << ",\n";
                first = false;
                jf << "    {\"path\": \"" << json_escape(p.first) << "\", \"rows\": " << p.second << "}";
            }
            jf << "\n  ],\n";
            jf << "  \"total_rows\": " << total << ",\n";
            jf << "  \"file_count\": " << csv_summary.size() << "\n";
            jf << "}\n";
            jf.close();
            logger::info(std::string("Wrote JSON summary: ") + jsonOut.string());
        }

        // Also emit CSV summary next to the JSON summary (name derived from JSON base)
        try {
            fs::path csvSummaryOut = jsonOut.parent_path() / (jsonOut.stem().string() + ".csv");
            std::ofstream cs(csvSummaryOut, std::ios::out | std::ios::trunc);
            if (cs) {
                cs << "path,rows\n";
                for (const auto &p : csv_summary) {
                    // minimal escaping for CSV summary
                    std::string pathEsc = p.first;
                    if (pathEsc.find(',') != std::string::npos || pathEsc.find('"') != std::string::npos) {
                        std::string tmp;
                        tmp.push_back('"');
                        for (char c : pathEsc) {
                            if (c == '"') tmp.append("\""); else tmp.push_back(c);
                        }
                        tmp.push_back('"');
                        pathEsc = tmp;
                    }
                    cs << pathEsc << ',' << p.second << "\n";
                }
                cs.close();
                logger::info(std::string("Wrote CSV summary: ") + csvSummaryOut.string());
            } else {
                logger::warn(std::string("Failed to open CSV summary for writing: ") + csvSummaryOut.string());
            }
        } catch (...) {
            logger::warn(std::string("Exception while writing CSV summary"));
        }
    }

#ifdef _WIN32
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
        logger::info(std::string("Готово. Данные в ") + db);
#endif
    } else {
        std::cout << "Done. Data in " << db << "\n";
        logger::info(std::string("Done. Data in ") + db);
    }
#endif
    return 0;
}