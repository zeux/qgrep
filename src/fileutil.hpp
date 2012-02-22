#ifndef TRAVERSE_HPP
#define TRAVERSE_HPP

#include <string>
#include <utility>
#include <stdint.h>

void traverseDirectory(const char* path, void (*callback)(void* context, const char* path), void* context = 0);
void createPath(const char* path);
bool renameFile(const char* oldpath, const char* newpath);
std::string replaceExtension(const char* path, const char* ext);

bool getFileAttributes(const char* path, uint64_t* mtime, uint64_t* size);
	
#endif
