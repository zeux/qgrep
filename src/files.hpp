#pragma once

#include <vector>
#include <string>

class Output;
struct FileInfo;

bool buildFiles(Output* output, const char* path, const std::vector<FileInfo>& files);

unsigned int searchFiles(Output* output, const char* file, const char* string, unsigned int options, unsigned int limit, const char* include, const char* exclude);