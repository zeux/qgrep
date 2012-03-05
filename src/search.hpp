#pragma once

enum SearchOptions
{
	SO_IGNORECASE = 1 << 0,
	SO_LITERAL = 1 << 1,
	SO_VISUALSTUDIO = 1 << 2,
	SO_COLUMNNUMBER = 1 << 3
};
	
void searchProject(const char* file, const char* string, unsigned int options);
