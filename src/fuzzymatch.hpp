#pragma once

#include <string>
#include <vector>
#include <utility>

class FuzzyMatcher
{
public:
	FuzzyMatcher(const char* query);

	bool match(const char* data, size_t size);
    int rank(const char* data, size_t size, int* positions = nullptr);

	size_t size() const
	{
		return cfquery.size();
	}

private:
	bool table[256];

	std::string cfquery;
    std::vector<std::pair<int, char>> buf;
    std::vector<int> cache;
    std::vector<int> cachepos;
};
