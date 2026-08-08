#pragma once
#include <string>
#include <vector>
#include "entry.h"

// Parses an Aoki "A26 Shortwave Frequency List"-style fixed-width text file
// into a vector of ListenInfoEntry. Column boundaries were measured directly
// against a real sample (see aoki_parser.cpp for the exact positions) —
// verified against LW/MW rows only so far. Malformed/unrecognized lines
// (including the title and header rows) are skipped and counted rather than
// aborting the whole import.
std::vector<ListenInfoEntry> parseAokiTxt(const std::string& path);
