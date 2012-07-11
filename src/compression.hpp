#pragma once

#include <vector>

std::vector<char> compress(const std::vector<char>& data);

void decompress(void* dest, size_t destSize, const void* source, size_t sourceSize);
