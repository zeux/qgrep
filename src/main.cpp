#include "output.hpp"
#include "init.hpp"
#include "build.hpp"
#include "update.hpp"
#include "search.hpp"
#include "project.hpp"
#include "files.hpp"
#include "stringutil.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cassert>
#include <cstdarg>
#include <mutex>

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

class StringOutput: public Output
{
public:
	StringOutput(std::string& buf): result(buf)
	{
	}

	virtual void print(const char* message, ...)
	{
		va_list l;
		va_start(l, message);
		strprintf(message, l);
		va_end(l);
	}

	virtual void error(const char* message, ...)
	{
		va_list l;
		va_start(l, message);
		strprintf(message, l);
		va_end(l);
	}

private:
	std::string& result;
	std::mutex mutex;

	void strprintf(const char* format, va_list args)
	{
		int count = _vsnprintf_c(0, 0, format, args);
		assert(count >= 0);

		if (count > 0)
		{
			std::unique_lock<std::mutex> lock(mutex);

			size_t offset = result.size();
			result.resize(offset + count);
			_vsnprintf(&result[offset], count, format, args);
		}
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

std::vector<std::string> getProjectPaths(const char* name)
{
	std::vector<std::string> result = strcmp(name, "*") == 0 ? getProjects() : split(name, [](char ch) { return ch == ','; });

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
		output->error("Uncaught exception: %s\n", e.what());
	}
}

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

void pinModule()
{
	static HMODULE module;
	static BOOL result = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&module, &module);
}

extern "C" __declspec(dllexport) void entryPointConsole(int argc, const char** argv)
{
	StandardOutput output;
	mainImpl(&output, argc, argv);
}

extern "C" __declspec(dllexport) const char* entryPointVim(const char* args)
{
	pinModule();

	std::vector<const char*> argv;
	argv.push_back("qgrep.dll");

	std::string argstr = args;
	argstr += '\n';

	size_t last = 0;

	for (size_t i = 0; i < argstr.size(); ++i)
		if (argstr[i] == '\n')
		{
			argstr[i] = 0;
			argv.push_back(argstr.c_str() + last);
			last = i + 1;
		}

	// string contents is preserved until next call
	static std::string result;
	result.clear();

	StringOutput output(result);
	mainImpl(&output, argv.size(), &argv[0]);

	return result.c_str();
}