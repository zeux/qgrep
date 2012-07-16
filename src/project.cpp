#include "common.hpp"
#include "project.hpp"

#include "output.hpp"
#include "fileutil.hpp"
#include "stringutil.hpp"
#include "regex.hpp"

#include <fstream>
#include <memory>
#include <algorithm>
#include <stdexcept>
#include <map>
#include <string>

static std::string getHomePath()
{
    char* qghome = getenv("QGREP_HOME");
    char* home = qghome ? qghome : getenv("HOME");
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
		if (n == "*" || n == "%" || n.back() == '/')
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

struct ProjectGroup
{
	ProjectGroup* parent;

	std::vector<std::string> paths;
	std::vector<std::string> files;
	std::shared_ptr<Regex> include;
	std::shared_ptr<Regex> exclude;

	std::vector<std::unique_ptr<ProjectGroup>> groups;
};

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

	if (str.compare(0, length, prefix) == 0 && (str.length() == length || isspace(str[length])))
	{
		suffix = trim(str.substr(length));
		return true;
	}

	return false;
}

static std::shared_ptr<Regex> createRegexCached(const std::string& query, std::map<std::string, std::shared_ptr<Regex>>& regexCache)
{
	auto p = regexCache.insert(std::make_pair(query, std::shared_ptr<Regex>()));

	if (p.second)
		p.first->second = std::shared_ptr<Regex>(createRegex(query.c_str(), RO_IGNORECASE));

	return p.first->second;
}

static std::shared_ptr<Regex> createOrRegexCached(const std::vector<std::string>& list, std::map<std::string, std::shared_ptr<Regex>>& regexCache)
{
	if (list.empty()) return std::shared_ptr<Regex>();
	
	std::string re = "(" + list[0] + ")";

	for (size_t i = 1; i < list.size(); ++i)
		re += "|(" + list[i] + ")";

	return createRegexCached(re, regexCache);
}

static std::unique_ptr<ProjectGroup> buildGroup(std::unique_ptr<ProjectGroup> group, const std::vector<std::string>& include, const std::vector<std::string>& exclude,
	std::map<std::string, std::shared_ptr<Regex>>& regexCache)
{
	group->include = createOrRegexCached(include, regexCache);
	group->exclude = createOrRegexCached(exclude, regexCache);
	return move(group);
}

static std::unique_ptr<ProjectGroup> parseGroup(std::ifstream& in, const char* file, unsigned int& lineId, ProjectGroup* parent,
	std::map<std::string, std::shared_ptr<Regex>>& regexCache, const char* pathBase)
{
	std::string line, suffix;
	std::vector<std::string> include, exclude;

	std::unique_ptr<ProjectGroup> result(new ProjectGroup);
	result->parent = parent;

	while (std::getline(in, line))
	{
		line = trim(line);
		lineId++;

		// parse lines
		if (line.empty() || line[0] == '#')
			continue; // skip comments
		else if (extractSuffix(line, "path", suffix))
		{
			if (suffix.empty()) throw std::runtime_error("No path specified");
			result->paths.push_back(normalizePath(pathBase, suffix.c_str()));
		}
		else if (extractSuffix(line, "file", suffix))
		{
			if (suffix.empty()) throw std::runtime_error("No path specified");
			result->files.push_back(normalizePath(pathBase, suffix.c_str()));
		}
		else if (extractSuffix(line, "include", suffix))
		{
			createRegexCached(suffix, regexCache);
			include.push_back(suffix);
		}
		else if (extractSuffix(line, "exclude", suffix))
		{
			createRegexCached(suffix, regexCache);
			exclude.push_back(suffix);
		}
		else if (extractSuffix(line, "group", suffix))
			result->groups.push_back(parseGroup(in, file, lineId, result.get(), regexCache, pathBase));
		else if (extractSuffix(line, "endgroup", suffix))
		{
			if (!parent) throw std::runtime_error("Mismatched endgroup");
			return buildGroup(move(result), include, exclude, regexCache);
		}
		else
		{
			// path without any special directive
			std::string path = normalizePath(pathBase, line.c_str());

			if (line.back() == '/' || line.back() == '\\')
				result->paths.push_back(path);
			else
				result->files.push_back(path);
		}
	}

	if (parent) throw std::runtime_error("End of file while looking for endgroup");
	return buildGroup(move(result), include, exclude, regexCache);
}

static std::unique_ptr<ProjectGroup> parseProject(Output* output, const char* file)
{
	std::ifstream in(file);
	if (!in)
	{
		output->error("Error reading file %s\n", file);
		return std::unique_ptr<ProjectGroup>();
	}

	// treat all project paths as project file-relative; treat project file path as current directory-relative
	std::string pathBase = normalizePath(getCurrentDirectory().c_str(), (std::string(file) + "/..").c_str());

	unsigned int line = 0;
	std::map<std::string, std::shared_ptr<Regex>> regexCache;

	try
	{
		return parseGroup(in, file, line, 0, regexCache, pathBase.c_str());
	}
	catch (const std::exception& e)
	{
		output->error("%s(%d): %s\n", file, line, e.what());
		return std::unique_ptr<ProjectGroup>();
	}
}

static bool isFileAcceptable(ProjectGroup* group, const char* path)
{
	size_t length = strlen(path);

	for (; group; group = group->parent)
	{
		if (group->include && !group->include->search(path, length))
			return false;

		if (group->exclude && group->exclude->search(path, length))
			return false;
	}

	return true;
}

static void getProjectGroupFiles(Output* output, ProjectGroup* group, std::vector<FileInfo>& files)
{
	for (auto& path: group->files)
	{
		uint64_t mtime, size;

		if (getFileAttributes(path.c_str(), &mtime, &size))
			files.push_back(FileInfo(path, mtime, size));
		else
			output->error("Error reading metadata for file %s\n", path.c_str());
	}

	for (auto& folder: group->paths)
	{
		std::string buf;

		bool result = traverseDirectoryMeta(folder.c_str(), [&](const char* path, uint64_t mtime, uint64_t size) { 
			if (isFileAcceptable(group, path))
			{
				joinPaths(buf, folder.c_str(), path);
				files.push_back(FileInfo(buf, mtime, size));
			}
		});

		if (!result) output->error("Error reading folder %s\n", folder.c_str());
	}

	for (auto& child: group->groups)
		getProjectGroupFiles(output, child.get(), files);
}

bool getProjectFiles(Output* output, const char* path, std::vector<FileInfo>& files)
{
	std::unique_ptr<ProjectGroup> group = parseProject(output, path);
	if (!group) return false;

	files.clear();
	
	getProjectGroupFiles(output, group.get(), files);

	std::sort(files.begin(), files.end(), [](const FileInfo& l, const FileInfo& r) { return l.path < r.path; });
	files.erase(std::unique(files.begin(), files.end(), [](const FileInfo& l, const FileInfo& r) { return l.path == r.path; }), files.end());

	return true;
}
