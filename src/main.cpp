#include "common.hpp"
#include "init.hpp"
#include "build.hpp"
#include "search.hpp"
#include "project.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

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

unsigned int parseSearchOptions(const char* opts)
{
	unsigned int result = 0;
	
	for (const char* s = opts; *s; ++s)
	{
		switch (*s)
		{
		case 'i':
			result |= SO_IGNORECASE;
			break;

		case 'l':
			result |= SO_LITERAL;
			break;
			
		case 'V':
			result |= SO_VISUALSTUDIO;
			break;

		case 'C':
			result |= SO_COLUMNNUMBER;
			break;
			
		default:
			fatal("Unknown search option '%c'\n", *s);
		}
	}
	
	return result;
}

std::vector<std::string> split(const char* str, char sep)
{
	std::vector<std::string> result;

	for (const char* i = str; i; )
	{
		const char* sp = strchr(i, sep);

		if (sp)
		{
			result.push_back(std::string(i, sp));
			i = sp + 1;
		}
		else
		{
			result.push_back(i);
			break;
		}
	}

	return result;
}

std::vector<std::string> getProjectPaths(const char* name)
{
	std::vector<std::string> result = strcmp(name, "*") == 0 ? getProjects() : split(name, ',');

	for (size_t i = 0; i < result.size(); ++i)
		result[i] = getProjectPath(result[i].c_str());

	return result;
}

int main(int argc, const char** argv)
{
	if (argc > 3 && strcmp(argv[1], "init") == 0)
	{
		initProject(argv[2], getProjectPath(argv[2]).c_str(), argv[3]);
	}
	else if (argc > 2 && strcmp(argv[1], "build") == 0)
	{
		std::vector<std::string> paths = getProjectPaths(argv[2]);

		for (size_t i = 0; i < paths.size(); ++i)
			buildProject(paths[i].c_str());
	}
	else if (argc > 3 && strcmp(argv[1], "search") == 0)
	{
		std::vector<std::string> paths = getProjectPaths(argv[2]);

		unsigned int options = 0;

		for (int i = 3; i + 1 < argc; ++i)
			options |= parseSearchOptions(argv[i]);

		for (size_t i = 0; i < paths.size(); ++i)
			searchProject(paths[i].c_str(), argv[argc - 1], options);
	}
	else if (argc > 1 && strcmp(argv[1], "projects") == 0)
	{
		std::vector<std::string> projects = getProjects();

		for (size_t i = 0; i < projects.size(); ++i)
			printf("%s\n", projects[i].c_str());
	}
	else
	{
		fatal("Usage:\n"
				"qgrep init <project> <path>\n"
				"qgrep build <project-list>\n"
				"qgrep search <project-list> <search-options> <query>\n"
				"qgrep projects\n"
				"\n"
				"<project> is either a project name (stored in ~/.qgrep) or a project path\n"
				"<project-list> is either * (all projects) or a comma-separated list of project names\n"
				"<search-options> can include:\n"
				"  i - case-insensitive search\n"
				"  l - literal search (query is treated as a literal string)\n"
				"  V - Visual Studio style formatting\n"
				"<query> is a regular expression\n"
				);
	}

}
