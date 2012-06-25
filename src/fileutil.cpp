#include "common.hpp"
#include "fileutil.hpp"

static bool isSeparator(char ch)
{
	return ch == '/' || ch == '\\';
}

void createPath(const char* path)
{
	std::string p = path;

	for (size_t i = 0; i < p.size(); ++i)
	{
		if (isSeparator(p[i]))
		{
			char ch = p[i];

			p[i] = 0;
			createDirectory(p.c_str());

			p[i] = ch;
		}
	}

	createDirectory(p.c_str());
}

void createPathForFile(const char* path)
{
	std::string p = path;

	std::string::size_type spos = p.find_last_of("/\\");
	if (spos != std::string::npos) p.erase(p.begin() + spos, p.end());

	createPath(p.c_str());
}

std::string replaceExtension(const char* path, const char* ext)
{
	std::string p = path;
	std::string::size_type pos = p.find_last_of("./\\");

	return (pos != std::string::npos && p[pos] == '.') ? p.substr(0, pos) + ext : p + ext;
}

bool traverseFileNeeded(const char* name)
{
	if (name[0] == '.')
	{
		// pseudo-folders
		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
	
		// VCS folders
		if (strcmp(name, ".svn") == 0 || strcmp(name, ".hg") == 0 || strcmp(name, ".git") == 0) return false;
	}

	return true;
}

void joinPaths(std::string& buf, const char* lhs, const char* rhs)
{
	buf = lhs;
	if (!buf.empty() && !isSeparator(buf.back()) && !isSeparator(*rhs)) buf += "/";
	buf += rhs;
}