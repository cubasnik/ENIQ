#include "logger.h"
#include <iostream>
#include <mutex>

static LogLevel g_level = LogLevel::Info;
static std::mutex g_log_mtx;
static bool g_json = false;

namespace logger {
    void set_level(LogLevel lvl) { g_level = lvl; }
    LogLevel get_level() { return g_level; }
    void set_json(bool on) { g_json = on; }
    bool is_json() { return g_json; }

    static void emit_plain(const char* prefix, const std::string& msg) {
        std::lock_guard<std::mutex> lg(g_log_mtx);
        std::cout << prefix << msg << std::endl;
    }

    static void emit_json(const char* level, const std::string& msg) {
        std::lock_guard<std::mutex> lg(g_log_mtx);
        // Minimal JSON: no timestamp for simplicity
        std::cout << "{\"level\":\"" << level << "\",\"msg\":\"";
        // escape quotes and backslashes in msg
        for (char c : msg) {
            if (c == '\\') std::cout << "\\\\";
            else if (c == '"') std::cout << "\\\"";
            else if (c == '\n') std::cout << "\\n";
            else std::cout << c;
        }
        std::cout << "\"}" << std::endl;
    }

    void error(const std::string& msg) { if ((int)g_level >= (int)LogLevel::Error) { if (g_json) emit_json("ERROR", msg); else emit_plain("ERROR: ", msg); } }
    void warn(const std::string& msg)  { if ((int)g_level >= (int)LogLevel::Warn)  { if (g_json) emit_json("WARN", msg);  else emit_plain("WARN: ", msg); } }
    void info(const std::string& msg)  { if ((int)g_level >= (int)LogLevel::Info)  { if (g_json) emit_json("INFO", msg);  else emit_plain("INFO: ", msg); } }
    void debug(const std::string& msg) { if ((int)g_level >= (int)LogLevel::Debug) { if (g_json) emit_json("DEBUG", msg); else emit_plain("DEBUG: ", msg); } }
}
