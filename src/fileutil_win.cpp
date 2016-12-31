#ifdef _WIN32

#include "common.hpp"
#include "fileutil.hpp"

#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

const size_t kMaxPathLength = 32768;

static std::wstring fromUtf8(const char* path)
{
	wchar_t buf[kMaxPathLength];
	size_t result = MultiByteToWideChar(CP_UTF8, 0, path, strlen(path), buf, ARRAYSIZE(buf));
	assert(result);

	return std::wstring(buf, result);
}

std::string toUtf8(const wchar_t* path)
{
	char buf[kMaxPathLength];
	size_t result = WideCharToMultiByte(CP_UTF8, 0, path, wcslen(path), buf, sizeof(buf), NULL, NULL);
	assert(result);

	return std::string(buf, result);
}

static uint64_t combine(uint32_t hi, uint32_t lo)
{
	return (static_cast<uint64_t>(hi) << 32) | lo;
}

static bool traverseDirectoryRec(const wchar_t* path, const char* relpath, const std::function<void (const char* name, uint64_t mtime, uint64_t size)>& callback)
{
	std::wstring query = path + std::wstring(L"/*");

	WIN32_FIND_DATAW data;
	HANDLE h = FindFirstFileW(query.c_str(), &data);

	if (h == INVALID_HANDLE_VALUE)
		return false;

	std::wstring buf;
	std::string relbuf;

	do
	{
		char filename[MAX_PATH];
		WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, filename, sizeof(filename), 0, 0);

		if (traverseFileNeeded(filename))
		{
			joinPaths(relbuf, relpath, filename);

			if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
			{
				// Skip reparse points to avoid handling cycles
			}
			else if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				buf = path;
				buf += '/';
				buf += data.cFileName;

				traverseDirectoryRec(buf.c_str(), relbuf.c_str(), callback);
			}
			else
			{
				uint64_t mtime = combine(data.ftLastWriteTime.dwHighDateTime, data.ftLastWriteTime.dwLowDateTime);
				uint64_t size = combine(data.nFileSizeHigh, data.nFileSizeLow);

				callback(relbuf.c_str(), mtime, size);
			}
		}
	}
	while (FindNextFileW(h, &data));

	FindClose(h);

	return true;
}

bool traverseDirectory(const char* path, const std::function<void (const char* name, uint64_t mtime, uint64_t size)>& callback)
{
	return traverseDirectoryRec(fromUtf8(path).c_str(), "", callback);
}

bool renameFile(const char* oldpath, const char* newpath)
{
	return !!MoveFileExW(fromUtf8(oldpath).c_str(), fromUtf8(newpath).c_str(), MOVEFILE_REPLACE_EXISTING);
}

bool getFileAttributes(const char* path, uint64_t* mtime, uint64_t* size)
{
	WIN32_FILE_ATTRIBUTE_DATA data;

	if (GetFileAttributesExW(fromUtf8(path).c_str(), GetFileExInfoStandard, &data))
	{
		*mtime = combine(data.ftLastWriteTime.dwHighDateTime, data.ftLastWriteTime.dwLowDateTime);
		*size = combine(data.nFileSizeHigh, data.nFileSizeLow);
		return true;
	}

	return false;
}

void createDirectory(const char* path)
{
    CreateDirectoryW(fromUtf8(path).c_str(), NULL);
}

std::string getCurrentDirectory()
{
	wchar_t buf[kMaxPathLength];
	GetCurrentDirectoryW(ARRAYSIZE(buf), buf);

	return toUtf8(buf);
}

FILE* openFile(const char* path, const char* mode)
{
	wchar_t wmode[8] = {};

	assert(strlen(mode) < ARRAYSIZE(wmode));
	std::copy(mode, mode + strlen(mode), wmode);

	return _wfopen(fromUtf8(path).c_str(), wmode);
}
#endif
