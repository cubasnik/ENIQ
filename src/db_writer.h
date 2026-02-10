#pragma once
#include <string>
#include <vector>
#include "xml_parser.h"

bool initDatabase(const std::string& dbPath);
bool saveRecords(const std::string& dbPath, const std::vector<CounterRecord>& records);

