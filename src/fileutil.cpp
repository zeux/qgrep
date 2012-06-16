#include "fileutil.hpp"

#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>

static uint64_t combine(uint32_t hi, uint32_t lo)
{
	return (static_cast<uint64_t>(hi) << 32) | lo;
}

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

static void concatPathName(std::string& buf, const char* path, const char* name)
{
	buf = path;
	if (*path) buf += "/";
	buf += name;
}

static bool readDirectory(const char* path, std::vector<WIN32_FIND_DATAA>& result)
{
	std::string query = std::string(path) + "/*";

	WIN32_FIND_DATAA data;
	HANDLE h = FindFirstFileA(query.c_str(), &data);

	if (h != INVALID_HANDLE_VALUE)
	{
		do result.push_back(data);
		while (FindNextFileA(h, &data));

		FindClose(h);
	}

	return h != INVALID_HANDLE_VALUE;
}

static bool traverseDirectoryImpl(const char* path, const char* relpath, const std::function<void (const char* name, uint64_t mtime, uint64_t size)>& callback)
{
	std::vector<WIN32_FIND_DATAA> contents;
	contents.reserve(16);

	if (readDirectory(path, contents))
	{
		std::string buf, relbuf;

		for (auto& data: contents)
		{
			if (processFile(data.cFileName))
			{
				concatPathName(relbuf, relpath, data.cFileName);

				if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					concatPathName(buf, path, data.cFileName);
					traverseDirectoryImpl(buf.c_str(), relbuf.c_str(), callback);
				}
				else
				{
					uint64_t mtime = combine(data.ftLastWriteTime.dwHighDateTime, data.ftLastWriteTime.dwLowDateTime);
					uint64_t size = combine(data.nFileSizeHigh, data.nFileSizeLow);
					callback(relbuf.c_str(), mtime, size);
				}
			}
		}

		return true;
	}

	return false;
}

bool traverseDirectory(const char* path, const std::function<void (const char* name)>& callback)
{
	return traverseDirectoryImpl(path, "", [&](const char* name, uint64_t, uint64_t) { callback(name); });
}

bool traverseDirectoryMeta(const char* path, const std::function<void (const char* name, uint64_t mtime, uint64_t size)>& callback)
{
	return traverseDirectoryImpl(path, "", callback);
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

void createPathForFile(const char* path)
{
	std::string p = path;

	std::string::size_type spos = p.find_last_of("/\\");
	if (spos != std::string::npos) p.erase(p.begin() + spos, p.end());

	createPath(p.c_str());
}

bool renameFile(const char* oldpath, const char* newpath)
{
	return !!MoveFileExA(oldpath, newpath, MOVEFILE_REPLACE_EXISTING);
}

std::string replaceExtension(const char* path, const char* ext)
{
	std::string p = path;
	std::string::size_type pos = p.find_last_of("./\\");

	return (pos != std::string::npos && p[pos] == '.') ? p.substr(0, pos) + ext : p + ext;
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