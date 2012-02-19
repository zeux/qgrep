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

static void traverseGetProject(void* context, const char* path)
{
	const char* dot = strrchr(path, '.');
	const char* slash = strrchr(path, '/');

	if (slash < dot && stricmp(dot, ".cfg") == 0)
	{
		static_cast<std::vector<std::string>*>(context)->push_back(std::string(slash + 1, dot));
	}
}

std::vector<std::string> getProjects()
{
	std::vector<std::string> result;

	std::string homePath = getHomePath();
	if (!homePath.empty()) traverseDirectory(homePath.c_str(), traverseGetProject, &result);

	return result;
}