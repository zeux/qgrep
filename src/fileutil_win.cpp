// This file is part of qgrep and is distributed under the MIT license, see LICENSE.md
#ifdef _WIN32

#include "common.hpp"
#include "fileutil.hpp"

#include <string>
#include <vector>
#include <algorithm>

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

static std::string toUtf8(const wchar_t* path, size_t length)
{
	char buf[kMaxPathLength];
	size_t result = WideCharToMultiByte(CP_UTF8, 0, path, length, buf, sizeof(buf), NULL, NULL);
	assert(result);

	return std::string(buf, result);
}

static std::string toUtf8(const wchar_t* path)
{
	return toUtf8(path, wcslen(path));
}

static uint64_t combine(uint32_t hi, uint32_t lo)
{
	return (static_cast<uint64_t>(hi) << 32) | lo;
}

static bool traverseDirectoryRec(const wchar_t* path, const char* relpath, const std::function<void (const char* name, uint64_t mtime, uint64_t size)>& callback)
{
	std::wstring query = path + std::wstring(L"/*");

	WIN32_FIND_DATAW data;
	HANDLE h = FindFirstFileExW(query.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);

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

bool removeFile(const char* path)
{
	return !!DeleteFileW(fromUtf8(path).c_str());
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

static bool isFullPath(const char* path)
{
	if (path[0] == '\\' && path[1] == '\\')
		return true; // UNC path

	if (((path[0] | 32) >= 'a' && (path[0] | 32) <= 'z') && path[1] == ':' && (path[2] == 0 || path[2] == '/' || path[2] == '\\'))
		return true; // drive path

	return false; // path relative to current directory
}

FILE* openFile(const char* path, const char* mode)
{
	// we need to get a full path to the file for relative paths (normalizePath will always work, isFullPath is an optimization)
	std::wstring wpath = fromUtf8(isFullPath(path) ? path : normalizePath(getCurrentDirectory().c_str(), path).c_str());

	// this makes sure we can use long paths and, more importantly, allows us to open files with special names such as 'aux.c'
	wpath.insert(0, L"\\\\?\\");
	std::replace(wpath.begin(), wpath.end(), '/', '\\');

	// convert file mode, assume short ASCII literal string
	wchar_t wmode[8] = {};
	assert(strlen(mode) < ARRAYSIZE(wmode));
	std::copy(mode, mode + strlen(mode), wmode);

	return _wfopen(wpath.c_str(), wmode);
}

bool watchDirectory(const char* path, const std::function<void (const char* name)>& callback)
{
	HANDLE h = CreateFileW(fromUtf8(path).c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if (h == INVALID_HANDLE_VALUE)
		return false;

	char buf[65536];
	DWORD bufsize = 0;

	unsigned int filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;

	while (ReadDirectoryChangesW(h, buf, sizeof(buf), true, filter, &bufsize, NULL, NULL))
	{
		if (bufsize == 0)
			continue;

		size_t offset = 0;

		for (;;)
		{
			assert(offset < bufsize);

			FILE_NOTIFY_INFORMATION* file = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buf + offset);

			if (file->Action == FILE_ACTION_ADDED || file->Action == FILE_ACTION_MODIFIED || file->Action == FILE_ACTION_REMOVED || file->Action == FILE_ACTION_RENAMED_NEW_NAME)
			{
				std::string fp = toUtf8(file->FileName, file->FileNameLength / sizeof(WCHAR));

				std::replace(fp.begin(), fp.end(), '\\', '/');

				callback(fp.c_str());
			}

			if (!file->NextEntryOffset)
				break;

			offset += file->NextEntryOffset;
		}
	}

	CloseHandle(h);

	return true;
}
#endif
