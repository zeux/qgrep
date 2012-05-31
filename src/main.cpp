#include "output.hpp"
#include "init.hpp"
#include "build.hpp"
#include "update.hpp"
#include "search.hpp"
#include "project.hpp"
#include "files.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

namespace re2 { int RunningOnValgrind() { return 0; } }

class StandardOutput: public Output
{
public:
	virtual void print(const char* message, ...)
	{
		va_list l;
		va_start(l, message);
		vfprintf(stdout, message, l);
		va_end(l);
	}

	virtual void error(const char* message, ...)
	{
		va_list l;
		va_start(l, message);
		vfprintf(stderr, message, l);
		va_end(l);
	}
};

unsigned int parseSearchFileOption(char opt)
{
	switch (opt)
	{
	case 'n':
		return SO_FILE_NAMEREGEX;

	case 'p':
		return SO_FILE_PATHREGEX;

	case 's':
		return 0; // default

	default:
		throw std::runtime_error(std::string("Unknown search option 'f") + opt + "'");
	}
}

std::pair<unsigned int, int> parseSearchOptions(const char* opts)
{
	unsigned int options = 0;
	int limit = -1;
	
	for (const char* s = opts; *s; ++s)
	{
		switch (*s)
		{
		case 'i':
			options |= SO_IGNORECASE;
			break;

		case 'l':
			options |= SO_LITERAL;
			break;
			
		case 'V':
			options |= SO_VISUALSTUDIO;
			break;

		case 'C':
			options |= SO_COLUMNNUMBER;
			break;

		case 'L':
			{
				char* end = 0;
				limit = strtol(s + 1, &end, 10);
				s = end - 1;
			}
			break;

		case 'f':
			s++;
			options |= parseSearchFileOption(*s);
			break;
			
		default:
			throw std::runtime_error(std::string("Unknown search option '") + *s + "'");
		}
	}
	
	return std::make_pair(options, limit);
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

void processSearchCommand(Output* output, int argc, const char** argv, void (*search)(Output*, const char*, const char*, unsigned int, unsigned int))
{
	std::vector<std::string> paths = getProjectPaths(argv[2]);

	unsigned int options = 0;
	unsigned int limit = 0;

	for (int i = 3; i + 1 < argc; ++i)
	{
		auto p = parseSearchOptions(argv[i]);

		options |= p.first;
		if (p.second >= 0) limit = p.second;
	}

	const char* query = argc > 3 ? argv[argc - 1] : "";

	for (size_t i = 0; i < paths.size(); ++i)
		search(output, paths[i].c_str(), query, options, limit);
}

void mainImpl(Output* output, int argc, const char** argv)
{
	try
	{
		if (argc > 3 && strcmp(argv[1], "init") == 0)
		{
			initProject(output, argv[2], getProjectPath(argv[2]).c_str(), argv[3]);
		}
		else if (argc > 2 && strcmp(argv[1], "build") == 0)
		{
			std::vector<std::string> paths = getProjectPaths(argv[2]);

			for (size_t i = 0; i < paths.size(); ++i)
				buildProject(output, paths[i].c_str());
		}
		else if (argc > 2 && strcmp(argv[1], "update") == 0)
		{
			std::vector<std::string> paths = getProjectPaths(argv[2]);

			for (size_t i = 0; i < paths.size(); ++i)
				updateProject(output, paths[i].c_str());
		}
		else if (argc > 3 && strcmp(argv[1], "search") == 0)
		{
			processSearchCommand(output, argc, argv, searchProject);
		}
		else if (argc > 2 && strcmp(argv[1], "files") == 0)
		{
			processSearchCommand(output, argc, argv, searchFiles);
		}
		else if (argc > 1 && strcmp(argv[1], "projects") == 0)
		{
			std::vector<std::string> projects = getProjects();

			for (size_t i = 0; i < projects.size(); ++i)
				output->print("%s\n", projects[i].c_str());
		}
		else
		{
			output->error("Usage:\n"
				"qgrep init <project> <path>\n"
				"qgrep build <project-list>\n"
				"qgrep search <project-list> <search-options> <query>\n"
				"qgrep files <project-list>\n"
				"qgrep files <project-list> <search-options> <query>\n"
				"qgrep projects\n"
				"\n"
				"<project> is either a project name (stored in ~/.qgrep) or a project path\n"
				"<project-list> is either * (all projects) or a comma-separated list of project names\n"
				"<search-options> can include:\n"
				"  i - case-insensitive search\n"
				"  l - literal search (query is treated as a literal string)\n"
				"  V - Visual Studio style formatting\n"
				"  C - include column name in output\n"
				"  Lnumber - limit output to <number> lines\n"
				"<search-options> can include additional options for file search:\n"
				"  fn - search in file names\n"
				"  fp - search in file paths\n"
				"  fs - search in file names/paths using a space-delimited literal query (default)\n"
				"       paths are grepped for components with slashes, names are grepped for the rest\n"
				"<query> is a regular expression\n"
				);
		}
	}
	catch (const std::exception& e)
	{
		output->error("%s\n", e.what());
	}
}

int main(int argc, const char** argv)
{
	StandardOutput output;
	mainImpl(&output, argc, argv);
}
