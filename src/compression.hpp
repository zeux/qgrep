#pragma once

#include <memory>
#include <utility>

std::pair<std::unique_ptr<char[]>, size_t> compress(const void* data, size_t dataSize, int level);

void decompress(void* dest, size_t destSize, const void* source, size_t sourceSize);
void decompressPartial(void* dest, size_t destSize, const void* source, size_t sourceSize, size_t targetSize);
