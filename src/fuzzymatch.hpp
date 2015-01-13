#pragma once

#include <string>
#include <vector>
#include <utility>

struct RankPathElement
{
	unsigned int position;
	char character;
	unsigned short score;

	RankPathElement()
		: position(0)
		, character(0)
		, score(0)
	{
	}

	RankPathElement(unsigned int position, char character, unsigned short score)
		: position(position)
		, character(character)
		, score(score)
	{
	}
};

class FuzzyMatcher
{
public:
	FuzzyMatcher(const char* query);

	bool match(const char* data, size_t size, int* positions = nullptr);
	int rank(const char* data, size_t size, int* positions = nullptr);

	size_t size() const
	{
		return cfquery.size();
	}

private:
	bool table[256];

	std::string cfquery;
	std::vector<RankPathElement> buf;
	std::vector<int> cache;
	std::vector<int> cachepos;
};
