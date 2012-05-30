#pragma once

#include <vector>
#include <string>

void buildFiles(const char* path);
void buildFiles(const char* path, const char** files, unsigned int count);
void buildFiles(const char* path, const std::vector<std::string>& files);

void searchFiles(const char* file, const char* string, unsigned int options);
