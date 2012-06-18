#pragma once

#include <stdint.h>

class Output;
struct DataChunkHeader;

class Builder
{
public:
	class BuilderImpl;

	Builder(Output* output, BuilderImpl* impl, unsigned int fileCount);
	~Builder();

	void appendFile(const char* path, uint64_t lastWriteTime, uint64_t fileSize);
	void appendFilePart(const char* path, unsigned int startLine, const void* data, size_t dataSize, uint64_t lastWriteTime, uint64_t fileSize);
	bool appendChunk(const DataChunkHeader& header, const void* compressedData, const void* index, bool firstFileIsSuffix);

private:
    BuilderImpl* impl;
	Output* output;
	unsigned int fileCount;
	uint64_t lastResultSize;

	void printStatistics();
};

Builder* createBuilder(Output* output, const char* path, unsigned int fileCount = 0);

void buildProject(Output* output, const char* path);