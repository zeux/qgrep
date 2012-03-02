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

void traverseDirectory(const char* path, const std::function<void (const char*)>& callback)
{
	WIN32_FIND_DATAA data;
	HANDLE h = FindFirstFileA((std::string(path) + "/*").c_str(), &data);

	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (processFile(data.cFileName))
			{
				std::string fp = path;
				fp += "/";
				fp += data.cFileName;
	
				if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					traverseDirectory(fp.c_str(), callback);
				else
					callback(fp.c_str());
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

bool renameFile(const char* oldpath, const char* newpath)
{
	return MoveFileExA(oldpath, newpath, MOVEFILE_REPLACE_EXISTING);
}

std::string replaceExtension(const char* path, const char* ext)
{
	const char* dot = strrchr(path, '.');

	return dot ? std::string(path, dot) + ext : std::string(path) + ext;
}

static uint64_t combine(uint32_t hi, uint32_t lo)
{
	return (static_cast<uint64_t>(hi) << 32) | lo;
}

bool getFileAttributes(const char* path, uint64_t* mtime, uint64_t* size)
{
	WIN32_FILE_ATTRIBUTE_DATA data;

	if (GetFileAttributesExA(path, GetFileExInfoStandard, &data))
	{
		*mtime = combine(data.ftLastWriteTime.dwHighDateTime, data.ftLastWriteTime.dwLowDateTime);
		*size = combine(data.nFileSizeHigh, data.nFileSizeLow);
		return true;
	}

	return false;
}