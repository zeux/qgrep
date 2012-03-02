#include "project.hpp"

#include "common.hpp"
#include "fileutil.hpp"
#include "regex.hpp"

#include <fstream>
#include <memory>
#include <algorithm>

static std::string getHomePath()
{
    char* home = getenv("HOME");
	const char* drive = getenv("HOMEDRIVE");
	const char* path = getenv("HOMEPATH");

    if (!home && !drive && !path) return "";

	return (home ? std::string(home) : std::string(drive) + path) + "/.qgrep";
}

std::string getProjectPath(const char* name)
{
	std::string home = getHomePath();

	if (home.empty()) return name;

	createPath(home.c_str());

	return home + "/" + name + ".cfg";
}

std::vector<std::string> getProjects()
{
	std::vector<std::string> result;

	std::string homePath = getHomePath();

	if (!homePath.empty())
	{
		traverseDirectory(homePath.c_str(), [&](const char* path) {
			const char* dot = strrchr(path, '.');
			const char* slash = strrchr(path, '/');

			if (slash < dot && strcmp(dot, ".cfg") == 0)
			{
				result.push_back(std::string(slash + 1, dot));
			}
		});
	}

	return result;
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

	return createRegex(re.c_str(), SO_IGNORECASE);
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

bool getProjectFiles(const char* path, std::vector<std::string>& files)
{
	std::vector<std::string> pathSet, includeSet, excludeSet, fileSet;

	if (!parseInput(path, pathSet, includeSet, excludeSet, fileSet))
	{
		error("Error opening project file %s for reading\n", path);
		return false;
	}

	std::unique_ptr<Regex> include(constructOrRE(includeSet));
	std::unique_ptr<Regex> exclude(constructOrRE(excludeSet));

	files = fileSet;

	for (size_t i = 0; i < pathSet.size(); ++i)
	{
		traverseDirectory(pathSet[i].c_str(), [&](const char* path) { 
			if (isFileAcceptable(include.get(), exclude.get(), path))
				files.push_back(path);
		});
	}

	std::sort(files.begin(), files.end());
	files.erase(std::unique(files.begin(), files.end()), files.end());

	return true;
}