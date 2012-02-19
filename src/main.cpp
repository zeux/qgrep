#include "common.hpp"
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
			
		case 'V':
			result |= SO_VISUALSTUDIO;
			break;
			
		default:
			fatal("Unknown search option '%c'\n", *s);
		}
	}
	
	return result;
}

std::vector<std::string> getProjectNames(const char* name)
{
	std::vector<std::string> result;

	if (strcmp(name, "*") == 0)
		result = getProjects();
	else if (strcmp(name, "?") == 0)
	{
		result = getProjects();
		if (!result.empty()) result.resize(1);
	}
	else
	{
		// comma-separated list
		for (const char* i = name; i; )
		{
			const char* c = strchr(i, ',');

			result.push_back(c ? std::string(i, c) : i);
			i = c ? c + 1 : 0;
		}
	}

	return result;
}

std::vector<std::string> getProjectPaths(const char* name)
{
	std::vector<std::string> result = getProjectNames(name);

	for (size_t i = 0; i < result.size(); ++i)
		result[i] = getProjectPath(result[i].c_str());

	return result;
}

int main(int argc, const char** argv)
{
	if (argc > 3 && strcmp(argv[1], "init") == 0)
	{
		initProject(getProjectPath(argv[2]).c_str(), argv[3]);
	}
	else if (argc > 2 && strcmp(argv[1], "build") == 0)
	{
		std::vector<std::string> paths = getProjectPaths(argv[2]);

		for (size_t i = 0; i < paths.size(); ++i)
			buildProject(paths[i].c_str());
	}
	else if (argc > 3 && strncmp(argv[1], "search", strlen("search")) == 0)
	{
		std::vector<std::string> paths = getProjectPaths(argv[2]);
		unsigned int options = parseSearchOptions(argv[1] + strlen("search"));

		for (size_t i = 0; i < paths.size(); ++i)
			searchProject(paths[i].c_str(), argv[3], options);
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
				"<project-list> is either * (all projects), ? (first project) or a comma-separated list of project names\n"
				"<query> is a regular expression\n"
				);
	}

}
