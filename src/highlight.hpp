#pragma once

#include <utility>
#include <string>
#include <vector>

// Use the default colors of the original grep
const char* const kHighlightMatch = "\033[;01;31m"; // bright red
const char* const kHighlightPath = "\033[;0;35m"; // magenta
const char* const kHighlightNumber = "\033[;0;32m"; // green
const char* const kHighlightSeparator = "\033[;0;36m"; // cyan
const char* const kHighlightEnd = "\033[0m";

// Compute highlighting for a string, given a set of ranges
typedef std::pair<size_t, size_t> HighlightRange;

void highlight(std::string& result, const char* data, size_t dataSize, HighlightRange* ranges, size_t rangeCount, const char* groupBegin, const char* groupEnd = kHighlightEnd);

// Highlighting helpers
void highlightRegex(std::vector<HighlightRange>& ranges, class Regex* re, const char* data, size_t size, const char* preparedRange = 0, size_t offset = 0, size_t basePosition = 0);

