#include "project.hpp"
#include "fileutil.hpp"

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

			if (slash < dot && stricmp(dot, ".cfg") == 0)
			{
				result.push_back(std::string(slash + 1, dot));
			}
		});
	}

	return result;
}