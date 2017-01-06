#pragma once

#include <string>
#include <functional>

#include <stdio.h>

bool traverseDirectory(const char* path, const std::function<void (const char* name, uint64_t mtime, uint64_t size)>& callback);
bool traverseFileNeeded(const char* name);

void createDirectory(const char* path);
void createPath(const char* path);
void createPathForFile(const char* path);

bool renameFile(const char* oldpath, const char* newpath);
bool removeFile(const char* path);

std::string getCurrentDirectory();

std::string replaceExtension(const char* path, const char* ext);
void joinPaths(std::string& buf, const char* lhs, const char* rhs);
std::string normalizePath(const char* base, const char* path);

bool getFileAttributes(const char* path, uint64_t* mtime, uint64_t* size);

FILE* openFile(const char* path, const char* mode);

bool watchDirectory(const char* path, const std::function<void (const char* name)>& callback);