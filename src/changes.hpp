// This file is part of qgrep and is distributed under the MIT license, see LICENSE.md
#pragma once

#include <vector>
#include <string>

class Output;

std::vector<std::string> readChanges(const char* path);
bool writeChanges(const char* path, const std::vector<std::string>& files);

void appendChanges(Output* output, const char* path, const std::vector<std::string>& files);
