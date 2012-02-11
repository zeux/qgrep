#include "fileutil.hpp"

#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>

static bool processFile(const char* name)
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

void traverseDirectory(const char* path, void (*callback)(void* context, const char* path), void* context)
{
	WIN32_FIND_DATAA data;
	HANDLE h = FindFirstFileA((std::string(path) + "\\*").c_str(), &data);

	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (processFile(data.cFileName))
			{
				std::string fp = path;
				fp += "\\";
				fp += data.cFileName;
	
				if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					traverseDirectory(fp.c_str(), callback, context);
				else
					callback(context, fp.c_str());
			}
		}
		while (FindNextFileA(h, &data));

		FindClose(h);
	}
}

void createPath(const char* path)
{
	std::string p = path;

	for (size_t i = 0; i < p.size(); ++i)
	{
		if (p[i] == '/' || p[i] == '\\')
		{
			char delimiter = p[i];

			p[i] = 0;
			mkdir(p.c_str());

			p[i] = delimiter;
		}
	}

	mkdir(p.c_str());
}
