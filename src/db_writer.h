#pragma once
#include <string>
#include <vector>
#include "xml_parser.h"

bool initDatabase(const std::string& dbPath);
bool saveRecords(const std::string& dbPath, const std::vector<CounterRecord>& records);

// Remove rows that look like test or malformed records (e.g. counter_name starts with 'unk_' or empty meas_type)
bool cleanTestRecords(const std::string& dbPath);

// Dry-run: collect records that would be removed by cleanTestRecords (no deletion)
bool dryRunCleanTestRecords(const std::string& dbPath, std::vector<CounterRecord>& out);

// If true, db_writer will prefer Russian messages when printing.
extern bool g_use_russian;

