// This file is part of qgrep and is distributed under the MIT license, see LICENSE.md
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
#include "highlight.hpp"
#include "filterutil.hpp"
#include "watch.hpp"
#include "changes.hpp"

#include <thread>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// isatty
#ifdef _WIN32
#	include <io.h>
#else
#	include <unistd.h>
#endif

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#endif

#include <mutex>
#include <chrono>

const char* kVersion = "1.2";

namespace re2 { bool RunningOnValgrind() { return false; } }

class StandardOutput: public Output
{
public:
	StandardOutput()
	{
		istty = isatty(fileno(stdout)) != 0;

	#ifndef _WIN32
		const char* term = getenv("TERM");

		istty = istty && term && strcmp(term, "dumb") != 0;
	#endif

	#ifdef _WIN32
		if (istty)
			SetConsoleOutputCP(CP_UTF8);
	#endif
	}

	virtual void rawprint(const char* data, size_t size)
	{
	#ifdef _WIN32
		if (istty)
			return printEscapeCodedStringToConsole(data, size);
	#endif

		fwrite(data, 1, size, stdout);
	}

	virtual void print(const char* message, ...)
	{
		va_list l;
		va_start(l, message);
		vfprintf(stdout, message, l);
		va_end(l);

		// treat \r as a newline as a form of line buffering
		if (istty && strchr(message, '\r'))
			fflush(stdout);
	}

	virtual void error(const char* message, ...)
	{
		va_list l;
		va_start(l, message);
		vfprintf(stderr, message, l);
		va_end(l);
	}

	virtual bool isTTY()
	{
		return istty;
	}

private:
	bool istty;
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

	case 'f':
		return SO_FILE_FUZZY;

	default:
		throw std::runtime_error("Unknown search option 'f" + std::string(opt != 0, opt) + "'");
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

const char* parseOrRegex(std::string& result, const char* str)
{
	const char* end = str;
	while (*end && *end != ' ') end++;

	if (!result.empty()) result += "|";
	result += "(";
	result += std::string(str, end);
	result += ")";

	return end;
}

void parseSearchOptions(const char* opts, unsigned int& options, unsigned int& limit, std::string& include, std::string& exclude)
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
			break;
			
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

		case 'S':
			options |= SO_SUMMARY;
			break;

		case 'f':
			s++;

			if (*s == 'i')
				s = parseOrRegex(include, s + 1) - 1;
			else if (*s == 'e')
				s = parseOrRegex(exclude, s + 1) - 1;
			else
				options |= parseSearchFileOption(*s);
			break;

		case ' ':
			break;
			
		default:
			throw std::runtime_error(std::string("Unknown search option '") + *s + "'");
		}
	}
}

std::tuple<unsigned int, unsigned int, std::string, std::string> getSearchOptions(int argc, const char** argv, int startarg, bool istty)
{
	unsigned int options = istty ? SO_HIGHLIGHT : 0;
	unsigned int limit = ~0u;
	std::string include, exclude;

	const char* gopts = getenv("QGREP_OPTIONS");

	// parse global options
	if (gopts)
		parseSearchOptions(gopts, options, limit, include, exclude);

	// parse command-line options
	for (int i = startarg; i + 1 < argc; ++i)
		parseSearchOptions(argv[i], options, limit, include, exclude);

	// choose default file search type
	if ((options & (SO_FILE_NAMEREGEX | SO_FILE_PATHREGEX | SO_FILE_VISUALASSIST | SO_FILE_FUZZY)) == 0)
		options |= SO_FILE_PATHREGEX;

	// highlighting includes match highlighting
	if (options & SO_HIGHLIGHT)
		options |= SO_HIGHLIGHT_MATCHES;

	// L0 means "no limit"
	if (limit == 0)
		limit = ~0u;

	return std::make_tuple(options, limit, include, exclude);
}

void processSearchCommand(Output* output, int argc, const char** argv, unsigned int (*search)(Output*, const char*, const char*, unsigned int, unsigned int, const char*, const char*))
{
	std::vector<std::string> paths = getProjectPaths(argv[2]);

	const char* query = argc > 3 ? argv[argc - 1] : "";

	unsigned int options, limit;
	std::string include, exclude;
	std::tie(options, limit, include, exclude) = getSearchOptions(argc, argv, 3, output->isTTY());

	if (*query == 0)
	{
		// There's no use highlighting matches from an empty query, and it substantially slows down output (since it matches on every character)
		options &= ~SO_HIGHLIGHT_MATCHES;
	}

	unsigned int total = 0;

	auto start = std::chrono::high_resolution_clock::now();

	for (size_t i = 0; limit > 0 && i < paths.size(); ++i)
	{
		unsigned int result = search(output, paths[i].c_str(), query, options, limit, include.empty() ? 0 : include.c_str(), exclude.empty() ? 0 : exclude.c_str());

		assert(result <= limit);
		limit -= result;
		total += result;
	}

	if (options & SO_SUMMARY)
	{
		auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start);

		output->print("Search complete, found %d%s matches in %.2f sec\n", total, (limit == 0 ? "+" : ""), static_cast<double>(time.count()) / 1000.0);
	}
}

void processFilterCommand(Output* output, int argc, const char** argv, const char* input, size_t inputSize)
{
	const char* query = argc > 2 ? argv[argc - 1] : "";

	unsigned int options, limit;
	std::string include, exclude;
	std::tie(options, limit, include, exclude) = getSearchOptions(argc, argv, 2, output->isTTY());

	if (input)
		filterBuffer(output, query, options, limit, input, inputSize);
	else
		filterStdin(output, query, options, limit);
}

void printHelp(Output* output, bool extended)
{
	output->print(
"qgrep %s (http://github.com/zeux/qgrep)\n"
"\n"
"Basic commands:\n"
"  qgrep init <project> <path>\n"
"  qgrep update <project-list>\n"
"  qgrep search <project-list> <search-options> <query>\n"
"  qgrep watch <project-list>\n"
"  qgrep intercative <project-list>\n"
"  qgrep help\n", kVersion);

    if (extended)
        output->print(
"\n"
"Advanced commands:\n"
"  qgrep build <project-list>\n"
"  qgrep change <project-list> <file-list>\n"
"  qgrep files <project-list>\n"
"  qgrep files <project-list> <search-options> <query>\n"
"  qgrep filter <search-options> <query>\n"
"  qgrep info <project-list>\n"
"  qgrep projects\n");

    output->print(
"\n"
"<project> is a project name (stored in ~/.qgrep) or a path to .cfg file\n"
"<project-list> is * or % (all projects) or a comma-separated list of names\n"
"<query> is a regular expression\n"
"\n"
"<search-options> can include:\n"
"  i - case-insensitive search          l - literal (substring) search\n"
"  V - Visual Studio formatting         S - print search summary\n");

    if (extended)
        output->print(
"  C - output match column number       L<num> - limit output to <num> lines\n"
"\n"
"<search-options> can include flags for restricting searches to certain files:\n"
"  fi<re> - only search in files with paths matching regex <re>\n"
"  fe<re> - don't search in files with paths matching regex <re>\n"
"\n"
"<search-options> can include additional options for output highlighting:\n"
"  H - force enable highlighting        HD - force disable highlighting\n"
"      (default for TTY output)         HM - only highlight search matches\n"  
"\n"
"<search-options> can include additional options for files/filter commands:\n"
"  fp - search in file paths (default)  fn - search in file names\n"
"  ff - fuzzy search with ranking       fs - search for space-delimited words\n"
"\n"
"in interactive mode, you can input 'search' and 'files' commands without a project list.\n");
}

void mainImpl(Output* output, int argc, const char** argv, const char* input, size_t inputSize)
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
		else if (argc > 1 && strcmp(argv[1], "filter") == 0)
		{
			processFilterCommand(output, argc, argv, input, inputSize);
		}
		else if (argc > 2 && strcmp(argv[1], "watch") == 0)
		{
			std::vector<std::string> paths = getProjectPaths(argv[2]);

			std::vector<std::thread> threads;

			for (size_t i = 0; i < paths.size(); ++i)
				threads.emplace_back([=] { watchProject(output, paths[i].c_str(), false); });

			for (auto& t: threads)
				t.join();
		}
		else if (argc > 2 && strcmp(argv[1], "change") == 0)
		{
			std::vector<std::string> paths = getProjectPaths(argv[2]);

			std::vector<std::string> changes(argv + 3, argv + argc);

			for (size_t i = 0; i < paths.size(); ++i)
				appendChanges(output, paths[i].c_str(), changes);
		}
		else if (argc > 2 && strcmp(argv[1], "interactive") == 0)
		{
			std::vector<std::string> paths = getProjectPaths(argv[2]);
			std::vector<std::thread> threads;

			for (size_t i = 0; i < paths.size(); ++i)
				threads.emplace_back([=] { watchProject(output, paths[i].c_str(), true); });

			std::vector<const char*> intArgv(argv, argv + argc);
			std::string intInput;
			intArgv.push_back(""); // Used later to place the input

			char buf[1024];
			while (fgets(buf, 1024, stdin))
			{
				if (strncmp(buf, "search ", 7) == 0)
				{
					intArgv[1] = "search";
					intInput = std::string(buf + 7, buf + strlen(buf) - 1);
					intArgv.back() = intInput.c_str();
					processSearchCommand(output, intArgv.size(), &intArgv[0], searchProject);
				}
				else if (strncmp(buf, "files ", 6) == 0)
				{
					intArgv[1] = "files";
					intInput = std::string(buf + 6, buf + strlen(buf) - 1);
					intArgv.back() = intInput.c_str();
					processSearchCommand(output, intArgv.size(), &intArgv[0], searchFiles);
				}
			}

			for (auto& t : threads)
				t.join();
		}
		else if (argc > 1 && strcmp(argv[1], "version") == 0)
		{
			output->print("%s\n", kVersion);
		}
		else
		{
			bool extended = argc > 1 && strcmp(argv[1], "help") == 0;

			printHelp(output, extended);
		}
	}
	catch (const std::exception& e)
	{
		output->error("Uncaught exception: %s\n", e.what());
	}
}

#ifdef _WIN32
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

int main(int argc, const char** argv)
{
	StandardOutput output;
	mainImpl(&output, argc, argv, 0, 0);
}

extern "C" DLLEXPORT const char* qgrepVim(const char* args)
{
	// make sure the DLL is not unloaded up until the process exit to speed up calls
	pinModule();

	size_t argsLength = strlen(args);

	const char* argsInput = strchr(args, '\2');
	if (argsInput) argsInput++;

	std::vector<const char*> argv;
	argv.push_back("qgrep");

	std::string argstr(args, argsInput ? argsInput - 1 : args + argsLength);
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
	mainImpl(&output, argv.size(), &argv[0], argsInput, args + argsLength - argsInput);

	return result.c_str();
}
