#pragma once

class Output;

enum SearchOptions
{
	SO_IGNORECASE = 1 << 0,
	SO_LITERAL = 1 << 1,
	SO_BRUTEFORCE = 1 << 2,

	SO_FILE_NAMEREGEX = 1 << 3,
	SO_FILE_PATHREGEX = 1 << 4,

	SO_VISUALSTUDIO = 1 << 5,
	SO_COLUMNNUMBER = 1 << 6,
};

unsigned int getRegexOptions(unsigned int options);
	
unsigned int searchProject(Output* output, const char* file, const char* string, unsigned int options, unsigned int limit);
