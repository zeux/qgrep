#ifndef COMMON_HPP
#define COMMON_HPP

void error(const char* message, ...);
void fatal(const char* message, ...);

enum SearchOptions
{
	SO_IGNORECASE = 1 << 0,
	SO_VISUALSTUDIO = 1 << 1
};
	
void initProject(const char* file, const char* path);
void buildProject(const char* file);
void searchProject(const char* file, const char* string, unsigned int options);

#endif
