#include "common.hpp"

#include "output.hpp"
#include "init.hpp"
#include "build.hpp"
#include "update.hpp"
#include "search.hpp"
#include "project.hpp"
#include "files.hpp"
#include "info.hpp"
#include "stringutil.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mutex>

namespace re2 { int RunningOnValgrind() { return 0; } }

class StandardOutput: public Output
{
public:
	virtual void rawprint(const char* data, size_t size)
	{
		fwrite(data, 1, size, stdout);
	}

	virtual void print(const char* message, ...)
	{
		va_list l;
		va_start(l, message);
		vfprintf(stdout, message, l);
		va_end(l);

		// treat \r as a newline as a form of line buffering
		if (strchr(message, '\r')) fflush(stdout);
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

	virtual void rawprint(const char* data, size_t size)
	{
		std::unique_lock<std::mutex> lock(mutex);

		result.insert(result.end(), data, data + size);
	}

	virtual void print(const char* message, ...)
	{
		std::unique_lock<std::mutex> lock(mutex);

		va_list l;
		va_start(l, message);
		strprintf(result, message, l);
		va_end(l);
	}

	virtual void error(const char* message, ...)
	{
		std::unique_lock<std::mutex> lock(mutex);

		va_list l;
		va_start(l, message);
		strprintf(result, message, l);
		va_end(l);
	}

private:
	std::string& result;
	std::mutex mutex;
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
		return SO_FILE_VISUALASSIST;

	case 't':
		return SO_FILE_COMMANDT_RANKED;

	case 'T':
		return SO_FILE_COMMANDT;

	default:
		throw std::runtime_error(std::string("Unknown search option 'f") + opt + "'");
	}
}

bool parseHighlightOption(char opt, unsigned int& options)
{
	switch (opt)
	{
	case 'D':
		options &= ~(SO_HIGHLIGHT | SO_HIGHLIGHT_MATCHES);
		return true;

	case 'M':
		options |= SO_HIGHLIGHT_MATCHES;
		return true;

	default:
		return false;
	}
}

void parseSearchOptions(const char* opts, unsigned int& options, unsigned int& limit)
{
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

		case 'b':
			options |= SO_BRUTEFORCE;
			
		case 'V':
			options |= SO_VISUALSTUDIO;
			break;

		case 'C':
			options |= SO_COLUMNNUMBER;
			break;

		case 'H':
			if (parseHighlightOption(s[1], options)) s++;
			else options |= SO_HIGHLIGHT;
			break;

		case 'L':
			{
				char* end = 0;
				limit = strtoul(s + 1, &end, 10);
				s = end - 1;
			}
			break;

		case 'f':
			s++;
			options |= parseSearchFileOption(*s);
			break;

		case ' ':
			break;
			
		default:
			throw std::runtime_error(std::string("Unknown search option '") + *s + "'");
		}
	}
}

std::pair<unsigned int, unsigned int> getSearchOptions(int argc, const char** argv)
{
	unsigned int options = 0;
	unsigned int limit = ~0u;

	const char* gopts = getenv("QGREP_OPTIONS");

	// parse global options
	if (gopts)
		parseSearchOptions(gopts, options, limit);

	// parse command-line options
	for (int i = 3; i + 1 < argc; ++i)
		parseSearchOptions(argv[i], options, limit);

	// choose default file search type
	if ((options & (SO_FILE_NAMEREGEX | SO_FILE_PATHREGEX | SO_FILE_VISUALASSIST | SO_FILE_COMMANDT | SO_FILE_COMMANDT_RANKED)) == 0)
		options |= SO_FILE_PATHREGEX;

	// highlighting includes match highlighting
	if (options & SO_HIGHLIGHT)
		options |= SO_HIGHLIGHT_MATCHES;

	// L0 means "no limit"
	if (limit == 0)
		limit = ~0u;

	return std::make_pair(options, limit);
}

void processSearchCommand(Output* output, int argc, const char** argv, unsigned int (*search)(Output*, const char*, const char*, unsigned int, unsigned int))
{
	std::vector<std::string> paths = getProjectPaths(argv[2]);

	const char* query = argc > 3 ? argv[argc - 1] : "";

	unsigned int options, limit;
	std::tie(options, limit) = getSearchOptions(argc, argv);

	if (*query == 0)
	{
		// There's no use highlighting matches from an empty query, and it substantially slows down output (since it matches on every character)
		options &= ~SO_HIGHLIGHT_MATCHES;
	}

	for (size_t i = 0; limit > 0 && i < paths.size(); ++i)
	{
		unsigned int result = search(output, paths[i].c_str(), query, options, limit);

		assert(result <= limit);
		limit -= result;
	}
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
		else if (argc > 2 && strcmp(argv[1], "info") == 0)
		{
			std::vector<std::string> paths = getProjectPaths(argv[2]);

			for (size_t i = 0; i < paths.size(); ++i)
			{
				if (i != 0) output->print("\n");
				printProjectInfo(output, paths[i].c_str());
			}
		}
		else
		{
			output->error("Usage:\n"
				"qgrep init <project> <path>\n"
				"qgrep build <project-list>\n"
				"qgrep search <project-list> <search-options> <query>\n"
				"qgrep files <project-list>\n"
				"qgrep files <project-list> <search-options> <query>\n"
				"qgrep info <project-list>\n"
				"qgrep projects\n"
				"\n"
				"<project> is either a project name (stored in ~/.qgrep) or a project path\n"
				"<project-list> is either * (all projects) or a comma-separated list of project names\n"
				"<search-options> can include:\n"
				"  i - case-insensitive search\n"
				"  l - literal search (query is treated as a literal string)\n"
				"  b - bruteforce search: skip indexing optimizations\n"
				"  V - Visual Studio style formatting\n"
				"  C - include column number in output\n"
				"  Lnumber - limit output to <number> lines\n"
				"<search-options> can include additional options for file search:\n"
				"  fp - search in file paths (default)\n"
				"  fn - search in file names\n"
				"  fs - search in file names/paths using a space-delimited literal query\n"
				"       paths are grepped for components with slashes, names are grepped for the rest\n"
				"  ft - search in file paths using a Command-T like fuzzy search with ranking\n"
				"  fT - search in file paths using a Command-T like fuzzy search without ranking\n"
				"<query> is a regular expression\n"
				);
		}
	}
	catch (const std::exception& e)
	{
		output->error("Uncaught exception: %s\n", e.what());
	}
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

static void pinModule()
{
	static HMODULE module;
	static BOOL result = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&module, &module);
}

#define DLLEXPORT __declspec(dllexport)
#else
static void pinModule()
{
}

#define DLLEXPORT
#endif

extern "C" DLLEXPORT void entryPointConsole(int argc, const char** argv)
{
	StandardOutput output;
	mainImpl(&output, argc, argv);
}

extern "C" DLLEXPORT const char* entryPointVim(const char* args)
{
	pinModule();

	std::vector<const char*> argv;
	argv.push_back("qgrep");

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
