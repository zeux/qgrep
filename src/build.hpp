#pragma once

#include <stdint.h>

class Builder
{
public:
	class BuilderImpl;

	Builder(BuilderImpl* impl, unsigned int fileCount);
	~Builder();

	void appendFile(const char* path);

private:
    BuilderImpl* impl;
	unsigned int fileCount;
	uint64_t lastResultSize;

	void printStatistics();
};

Builder* createBuilder(const char* path, unsigned int fileCount = 0);

void buildProject(const char* path);
