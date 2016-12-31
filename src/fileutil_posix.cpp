#ifndef _WIN32

#include "common.hpp"
#include "fileutil.hpp"

#include <string>
#include <vector>

#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

static int getFileType(const char* path)
{
	struct stat st;

	if (lstat(path, &st) == 0)
		return IFTODT(st.st_mode);

	return DT_UNKNOWN;
}

static bool traverseDirectoryRec(const char* path, const char* relpath, const std::function<void (const char* name, uint64_t mtime, uint64_t size)>& callback, bool needsStat)
{
	DIR* dir = opendir(path);

	if (!dir)
		return false;

	std::string buf, relbuf;

	while (dirent* entry = readdir(dir))
	{
		const dirent& data = *entry;

		if (traverseFileNeeded(data.d_name))
		{
			joinPaths(relbuf, relpath, data.d_name);
			joinPaths(buf, path, data.d_name);

			int type = (data.d_type == DT_UNKNOWN) ? getFileType(buf.c_str()) : data.d_type;

			if (type == DT_DIR)
			{
				traverseDirectoryRec(buf.c_str(), relbuf.c_str(), callback, needsStat);
			}
			else if (type == DT_REG)
			{
				uint64_t mtime = 0, size = 0;

				if (needsStat)
					getFileAttributes(buf.c_str(), &mtime, &size);

				callback(relbuf.c_str(), mtime, size);
			}
			else if (type == DT_LNK)
			{
				// Skip symbolic links to avoid handling cycles
			}
		}
	}

	closedir(dir);

	return true;
}

bool traverseDirectory(const char* path, const std::function<void (const char* name)>& callback)
{
	return traverseDirectoryRec(path, "", [&](const char* name, uint64_t, uint64_t) { callback(name); }, false);
}

bool traverseDirectoryMeta(const char* path, const std::function<void (const char* name, uint64_t mtime, uint64_t size)>& callback)
{
	return traverseDirectoryRec(path, "", callback, true);
}

bool renameFile(const char* oldpath, const char* newpath)
{
	return rename(oldpath, newpath) == 0;
}

bool getFileAttributes(const char* path, uint64_t* mtime, uint64_t* size)
{
	struct stat st;

	if (stat(path, &st) == 0)
	{
		*mtime = st.st_mtime;
		*size = st.st_size;
		return true;
	}

	return false;
}

void createDirectory(const char* path)
{
	mkdir(path, 0755);
}

std::string getCurrentDirectory()
{
    long length = pathconf(".", _PC_PATH_MAX);
    if (length <= 0) return "";

    std::string result;
    result.resize(length);

    const char* path = getcwd(&result[0], length);
    if (!path) return "";

    result.resize(strlen(path));

    return result;
}

FILE* openFile(const char* path, const char* mode)
{
	return fopen(path, mode);
}
#endif
