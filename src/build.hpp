#pragma once

#include <memory>

class Output;
struct DataChunkHeader;

struct BuildContext;

BuildContext* buildStart(Output* output, const char* path, unsigned int fileCount = 0);

void buildAppendFilePart(BuildContext* context, const char* path, unsigned int startLine, const char* data, size_t dataSize, uint64_t timeStamp, uint64_t fileSize);
bool buildAppendFile(BuildContext* context, const char* path, uint64_t timeStamp, uint64_t fileSize);
bool buildAppendChunk(BuildContext* context, const DataChunkHeader& header, std::unique_ptr<char[]>& compressedData, std::unique_ptr<char[]>& index, std::unique_ptr<char[]>& extra, bool firstFileIsSuffix);

unsigned int buildFinish(BuildContext* context);

void buildProject(Output* output, const char* path);
