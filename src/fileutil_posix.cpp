#ifndef _WIN32

#include "common.hpp"
#include "fileutil.hpp"

#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool traverseDirectoryRec(const char* path, const char* relpath, const std::function<void (const char* name, uint64_t mtime, uint64_t size)>& callback)
{
	int fd = open(path, O_DIRECTORY);
	DIR* dir = fdopendir(fd);

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

			struct stat st = {};
			int type = data.d_type;

			// we need to stat DT_UNKNOWN to be able to tell the type, and we need to stat files to get mtime/size
			if (type == DT_UNKNOWN || type == DT_REG)
			{
			#ifdef _ATFILE_SOURCE
				fstatat(fd, data.d_name, &st, 0);
			#else
				lstat(buf.c_str(), &st);
			#endif

				assert(type == DT_UNKNOWN || type == IFTODT(st.st_mode));
				type = IFTODT(st.st_mode);
			}

			if (type == DT_DIR)
			{
				traverseDirectoryRec(buf.c_str(), relbuf.c_str(), callback);
			}
			else if (type == DT_REG)
			{
				callback(relbuf.c_str(), st.st_mtime, st.st_size);
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

bool traverseDirectory(const char* path, const std::function<void (const char* name, uint64_t mtime, uint64_t size)>& callback)
{
	return traverseDirectoryRec(path, "", callback);
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
