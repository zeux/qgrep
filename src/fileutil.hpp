#ifndef TRAVERSE_HPP
#define TRAVERSE_HPP

void traverseDirectory(const char* path, void (*callback)(void* context, const char* path), void* context = 0);
void createPath(const char* path);
	
#endif
