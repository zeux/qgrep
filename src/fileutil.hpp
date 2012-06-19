#pragma once

#include <string>
#include <functional>

bool traverseDirectory(const char* path, const std::function<void (const char* name)>& callback);
bool traverseDirectoryMeta(const char* path, const std::function<void (const char* name, uint64_t mtime, uint64_t size)>& callback);

void createDirectory(const char* path);
void createPath(const char* path);
void createPathForFile(const char* path);
bool renameFile(const char* oldpath, const char* newpath);
std::string replaceExtension(const char* path, const char* ext);

bool getFileAttributes(const char* path, uint64_t* mtime, uint64_t* size);
