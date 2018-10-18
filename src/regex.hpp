#pragma once

#include <vector>
#include <string>

enum RegexOptions
{
	RO_IGNORECASE = 1 << 0,
	RO_LITERAL = 1 << 1,
};

struct RegexMatch
{
	const char* data;
	size_t size;

	RegexMatch();
	RegexMatch(const char* data, size_t size);

	operator bool() const;
};

class Regex
{
public:
	virtual ~Regex() {}

	virtual const char* rangePrepare(const char* data, size_t size) = 0;
	virtual RegexMatch rangeSearch(const char* data, size_t size) = 0;
	virtual void rangeFinalize(const char* data) = 0;

	virtual RegexMatch search(const char* data, size_t size) = 0;

	virtual std::vector<std::string> prefilterPrepare() = 0;
	virtual bool prefilterMatch(const std::vector<int>& matches) = 0;
};

Regex* createRegex(const char* pattern, unsigned int options);