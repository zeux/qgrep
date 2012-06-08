#pragma once

#include <vector>
#include <string>

class Output;

void buildFiles(Output* output, const char* path);
void buildFiles(Output* output, const char* path, const char** files, unsigned int count);
void buildFiles(Output* output, const char* path, const std::vector<std::string>& files);

unsigned int searchFiles(Output* output, const char* file, const char* string, unsigned int options, unsigned int limit);