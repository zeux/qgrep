#pragma once

#include <memory>

class Output;
struct DataChunkHeader;

class Builder
{
public:
	class BuilderImpl;

	Builder(Output* output, BuilderImpl* impl);
	~Builder();

	void appendFile(const char* path, uint64_t lastWriteTime, uint64_t fileSize);
	void appendFilePart(const char* path, unsigned int startLine, const void* data, size_t dataSize, uint64_t lastWriteTime, uint64_t fileSize);
	bool appendChunk(const DataChunkHeader& header, std::unique_ptr<char[]>& compressedData, std::unique_ptr<char[]>& index, std::unique_ptr<char[]>& extra, bool firstFileIsSuffix);

	unsigned int finish();

private:
	BuilderImpl* impl;
	Output* output;
};

Builder* createBuilder(Output* output, const char* path, unsigned int fileCount = 0);

void buildProject(Output* output, const char* path);
