#include "fileutil.hpp"

void createPath(const char* path)
{
	std::string p = path;

	for (size_t i = 0; i < p.size(); ++i)
	{
		if (p[i] == '/' || p[i] == '\\')
		{
			char delimiter = p[i];

			p[i] = 0;
			createDirectory(p.c_str());

			p[i] = delimiter;
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
