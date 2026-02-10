#pragma once
#include <string>
#include <vector>
#include "xml_parser.h"

bool initDatabase(const std::string& dbPath);
bool saveRecords(const std::string& dbPath, const std::vector<CounterRecord>& records);

// If true, db_writer will prefer Russian messages when printing.
extern bool g_use_russian;

