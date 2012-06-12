#include "project.hpp"

#include "output.hpp"
#include "fileutil.hpp"
#include "stringutil.hpp"
#include "regex.hpp"

#include <fstream>
#include <memory>
#include <algorithm>
#include <cassert>

static std::string getHomePath()
{
    char* home = getenv("HOME");
	const char* drive = getenv("HOMEDRIVE");
	const char* path = getenv("HOMEPATH");

    if (!home && !drive && !path) return "";

	return (home ? std::string(home) : std::string(drive) + path) + "/.qgrep";
}

static bool isSlash(char ch)
{
	return ch == '/' || ch == '\\';
}

static bool isFilePath(const char* name)
{
	return isSlash(name[0]) || (name[0] == '.' && isSlash(name[1])) || (isalpha(name[0]) && name[1] == ':' && isSlash(name[3]));
}

std::string getProjectPath(const char* name)
{
	if (isFilePath(name))
		return replaceExtension(name, ".cfg");
	else
	{
		std::string home = getHomePath();

		if (home.empty())
			return name;
		else
			return home + "/" + name + ".cfg";
	}
}

static std::vector<std::string> getProjectsByPrefix(const char* prefix)
{
	std::vector<std::string> result;

	std::string homePath = getHomePath();

	if (!homePath.empty())
	{
		std::string path = homePath;
		if (*prefix) (path += "/") += prefix;

		std::string pprefix = *prefix ? std::string(prefix) + "/" : "";

		traverseDirectory(path.c_str(), [&](const char* path) {
			const char* dot = strrchr(path, '.');

			if (strcmp(dot, ".cfg") == 0)
			{
				result.push_back(pprefix + std::string(path, dot));
			}
		});
	}

	return result;
}

std::vector<std::string> getProjects()
{
	return getProjectsByPrefix("");
}

std::vector<std::string> getProjectPaths(const char* name)
{
	// grab all names
	std::vector<std::string> names;
	
	for (std::string n: split(name, [](char ch) { return ch == ','; }))
	{
		if (n == "*" || n.back() == '/')
		{
			auto projects = getProjectsByPrefix(n.back() == '/' ? n.substr(0, n.length() - 1).c_str() : "");
			names.insert(names.end(), projects.begin(), projects.end());
		}
		else
		{
			names.push_back(n);
		}
	}

	// convert names to paths
	std::vector<std::string> paths;

	for (auto& n: names)
		paths.push_back(getProjectPath(n.c_str()));

	return paths;
}

static std::string trim(const std::string& s)
{
	const char* pattern = " \t";

	std::string::size_type b = s.find_first_not_of(pattern);
	std::string::size_type e = s.find_last_not_of(pattern);

	return (b == std::string::npos || e == std::string::npos) ? "" : s.substr(b, e + 1);
}

static bool extractSuffix(const std::string& str, const char* prefix, std::string& suffix)
{
	size_t length = strlen(prefix);

	if (str.compare(0, length, prefix) == 0 && str.length() > length && isspace(str[length]))
	{
		suffix = trim(str.substr(length));
		return true;
	}

	return false;
}

static bool parseInput(const char* file, std::vector<std::string>& paths, std::vector<std::string>& include, std::vector<std::string>& exclude, std::vector<std::string>& files)
{
	std::ifstream in(file);
	if (!in) return false;

	std::string line;
	std::string suffix;

	while (std::getline(in, line))
	{
		// remove comment
		std::string::size_type shp = line.find('#');
		if (shp != std::string::npos) line.erase(line.begin() + shp, line.end());

		// parse lines
		if (extractSuffix(line, "path", suffix))
			paths.push_back(suffix);
		else if (extractSuffix(line, "include", suffix))
			include.push_back(suffix);
		else if (extractSuffix(line, "exclude", suffix))
			exclude.push_back(suffix);
		else
		{
			std::string file = trim(line);
			if (!file.empty()) files.push_back(file);
		}
	}

	return true;
}

static Regex* constructOrRE(const std::vector<std::string>& list)
{
	if (list.empty()) return 0;
	
	std::string re = "(" + list[0] + ")";

	for (size_t i = 1; i < list.size(); ++i)
		re += "|(" + list[i] + ")";

	return createRegex(re.c_str(), RO_IGNORECASE);
}

static bool isFileAcceptable(Regex* include, Regex* exclude, const char* path)
{
	size_t length = strlen(path);

	if (include && !include->search(path, length))
		return false;

	if (exclude && exclude->search(path, length))
		return false;

	return true;
}

bool getProjectFiles(Output* output, const char* path, std::vector<std::string>& files)
{
	std::vector<std::string> pathSet, includeSet, excludeSet, fileSet;

	if (!parseInput(path, pathSet, includeSet, excludeSet, fileSet))
	{
		output->error("Error opening project file %s for reading\n", path);
		return false;
	}

	std::unique_ptr<Regex> include(constructOrRE(includeSet));
	std::unique_ptr<Regex> exclude(constructOrRE(excludeSet));

	files = fileSet;

	for (size_t i = 0; i < pathSet.size(); ++i)
	{
		std::string pathPrefix = pathSet[i] + "/";

		traverseDirectory(pathSet[i].c_str(), [&](const char* path) { 
			if (isFileAcceptable(include.get(), exclude.get(), path))
				files.push_back(pathPrefix + path);
		});
	}

	std::sort(files.begin(), files.end());
	files.erase(std::unique(files.begin(), files.end()), files.end());

	return true;
}