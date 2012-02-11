#include "qrep.hpp"
#include "fileutil.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <string>

namespace re2 { int RunningOnValgrind() { return 0; } }

void error(const char* message, ...)
{
	va_list l;
	va_start(l, message);
	vfprintf(stderr, message, l);
	va_end(l);
}

void fatal(const char* message, ...)
{
	va_list l;
	va_start(l, message);
	vfprintf(stderr, message, l);
	va_end(l);
	exit(1);
}

std::string getHomePath(const char* name)
{
    char* home = getenv("HOME");
	const char* drive = getenv("HOMEDRIVE");
	const char* path = getenv("HOMEPATH");

    if (!home && !drive && !path) return "";

	return (home ? std::string(home) : std::string(drive) + path) + "/.qrep";
}

std::string getProjectPath(const char* name)
{
	std::string home = getHomePath(name);

	if (home.empty()) return name;

	createPath(home.c_str());

	return home + "/" + name + ".cfg";
}

int main(int argc, const char** argv)
{
	if (argc < 2)
	{
		fatal("Usage:\n"
				"qrep build <rulefile>\n");
	}

	if (argc > 2 && strcmp(argv[1], "build") == 0)
		build(getProjectPath(argv[2]).c_str());
}
