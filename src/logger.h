#pragma once
#include <string>

enum class LogLevel { Error = 0, Warn = 1, Info = 2, Debug = 3 };

namespace logger {
    void set_level(LogLevel lvl);
    LogLevel get_level();
    void set_json(bool on);
    bool is_json();
    void error(const std::string& msg);
    void warn(const std::string& msg);
    void info(const std::string& msg);
    void debug(const std::string& msg);
}
