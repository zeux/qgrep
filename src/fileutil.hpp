#pragma once

#include <string>
#include <functional>
#include <stdint.h>

void traverseDirectory(const char* path, const std::function<void (const char*)>& callback);
void createPath(const char* path);
bool renameFile(const char* oldpath, const char* newpath);
std::string replaceExtension(const char* path, const char* ext);

bool getFileAttributes(const char* path, uint64_t* mtime, uint64_t* size);