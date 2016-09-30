#ifdef _WIN32

#include "common.hpp"
#include "fileutil.hpp"

#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static uint64_t combine(uint32_t hi, uint32_t lo)
{
	return (static_cast<uint64_t>(hi) << 32) | lo;
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
			if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
				traverseFileNeeded(data.cFileName))
			{
				joinPaths(relbuf, relpath, data.cFileName);

				if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					joinPaths(buf, path, data.cFileName);
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

bool renameFile(const char* oldpath, const char* newpath)
{
	return !!MoveFileExA(oldpath, newpath, MOVEFILE_REPLACE_EXISTING);
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

void createDirectory(const char* path)
{
    CreateDirectoryA(path, NULL);
}

std::string getCurrentDirectory()
{
    DWORD length = GetCurrentDirectoryA(0, NULL);
    if (length == 0) return "";

    std::string result;
	result.resize(length);
    result.resize(GetCurrentDirectoryA(length, &result[0]));

    return result;
}

#endif
