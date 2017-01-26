#pragma once

#include <vector>
#include <string>

std::vector<std::string> readChanges(const char* path);
bool writeChanges(const char* path, const std::vector<std::string>& files);
