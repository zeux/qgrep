#pragma once

void error(const char* message, ...);
void fatal(const char* message, ...);

enum SearchOptions
{
	SO_IGNORECASE = 1 << 0,
	SO_LITERAL = 1 << 1,
	SO_VISUALSTUDIO = 1 << 2
};
	
void initProject(const char* name, const char* file, const char* path);
void buildProject(const char* path);
void searchProject(const char* file, const char* string, unsigned int options);