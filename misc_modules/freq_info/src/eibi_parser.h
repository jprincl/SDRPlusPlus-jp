#pragma once
#include <string>
#include <vector>
#include "entry.h"

// Parses an EiBi "sked-Xzz.csv" file (semicolon-separated, 11 fields per
// line per eibispace.de/dx/README.TXT) into a vector of ListenInfoEntry.
// Entries with persistence code 8 (inactive) are silently dropped, same
// as EiBi's own tooling does for the freq/bc files. Malformed lines are
// skipped and counted rather than aborting the whole import.
//
// Returns entries in FILE order (not yet frequency-sorted) — the caller
// merges this with other sources and sorts once, per our earlier plan.
std::vector<ListenInfoEntry> parseEibiCsv(const std::string& path);
