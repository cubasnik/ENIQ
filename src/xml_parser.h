#pragma once
#include <string>
#include <vector>

struct CounterRecord {
    std::string timestamp;
    std::string mo_ldn;
    std::string meas_type;
    std::string counter_name;
    double value = 0.0;
};

// Parse EricssonSoft PM XML at `xmlPath` and append found records to `records`.
// Returns true on success.
bool parse_ericssonsoft_pm_xml(const std::string& xmlPath, std::vector<CounterRecord>& records);
