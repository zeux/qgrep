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

#ifdef __linux__
#include <sys/inotify.h>
#endif

#ifdef __APPLE__
#include <CoreServices/CoreServices.h>
#endif

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

				assert(type == DT_UNKNOWN || type == int(IFTODT(st.st_mode)));
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

bool removeFile(const char* path)
{
	return unlink(path) == 0;
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

#ifdef __linux__
static void addWatchRec(int fd, const char* path, const char* relpath, std::vector<std::string>& paths)
{
	DIR* dir = opendir(path);

	if (!dir)
		return;

    int wd = inotify_add_watch(fd, path, IN_CLOSE_WRITE | IN_MOVED_TO);

    if (wd >= 0)
    {
		if (paths.size() <= size_t(wd))
			paths.resize(wd + 1);

		paths[wd] = relpath;
    }

	std::string buf, relbuf;

	while (dirent* entry = readdir(dir))
	{
		const dirent& data = *entry;

		if (traverseFileNeeded(data.d_name) && data.d_type == DT_DIR)
		{
			joinPaths(relbuf, relpath, data.d_name);
			joinPaths(buf, path, data.d_name);

			addWatchRec(fd, buf.c_str(), relbuf.c_str(), paths);
		}
    }

    closedir(dir);
}

static bool watchDirectoryInotify(const char* path, const std::function<void (const char* name)>& callback)
{
    int fd = inotify_init();
    if (fd < 0)
		return false;

	std::vector<std::string> paths;
    addWatchRec(fd, path, "", paths);

	std::string relbuf;

    for (;;)
    {
        alignas(inotify_event) char buf[4096];
        ssize_t bufsize = read(fd, buf, sizeof(buf));

        if (bufsize <= 0)
            break;

        size_t offset = 0;
        inotify_event* e = reinterpret_cast<inotify_event*>(buf);

        while (offset + sizeof(inotify_event) + e->len <= size_t(bufsize))
        {
            if (traverseFileNeeded(e->name) && (e->mask & IN_ISDIR) == 0 && size_t(e->wd) < paths.size())
            {
				joinPaths(relbuf, paths[e->wd].c_str(), e->name);

				callback(relbuf.c_str());
            }

            offset += sizeof(inotify_event) + e->len;
            e = reinterpret_cast<inotify_event*>(buf + offset);
        }
    }

    close(fd);

	return true;
}
#endif

#ifdef __APPLE__
struct WatchContext
{
	std::string rootPath;

	std::function<void (const char* name)> callback;
};

static void watchCallback(ConstFSEventStreamRef streamRef, void* callbackContext, size_t numEvents, void* eventPaths, const FSEventStreamEventFlags eventFlags[], const FSEventStreamEventId eventIds[])
{
	WatchContext* context = static_cast<WatchContext*>(callbackContext);

    for (size_t i = 0; i < numEvents; ++i)
    {
        const char* path = static_cast<char**>(eventPaths)[i];
        unsigned int flag = eventFlags[i];

        if (flag & kFSEventStreamEventFlagItemIsFile)
        {
            if (flag & (kFSEventStreamEventFlagItemModified | kFSEventStreamEventFlagItemRenamed))
            {
				if (strncmp(path, context->rootPath.c_str(), context->rootPath.size()) == 0)
				{
					const char* relativePath = path + context->rootPath.size();

					context->callback(relativePath);
				}
            }
        }
    }
}

static bool watchDirectoryFSEvent(const char* path, const std::function<void (const char* name)>& callback)
{
    CFStringRef cpath = CFStringCreateWithCString(nullptr, path, kCFStringEncodingUTF8);
    CFArrayRef cpaths = CFArrayCreate(nullptr, reinterpret_cast<const void**>(&cpath), 1, nullptr);

	std::string rootPath = path;
	if (rootPath.back() != '/')
		rootPath += '/';

	WatchContext context = { rootPath, callback };
	FSEventStreamContext callbackContext = {};
	callbackContext.info = &context;

    FSEventStreamRef stream = FSEventStreamCreate(nullptr, watchCallback, &callbackContext, cpaths, kFSEventStreamEventIdSinceNow, 0.1, kFSEventStreamCreateFlagFileEvents);

    FSEventStreamScheduleWithRunLoop(stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    FSEventStreamStart(stream);

    CFRunLoopRun();

	FSEventStreamStop(stream);
	FSEventStreamInvalidate(stream);

    FSEventStreamRelease(stream);
    CFRelease(cpaths);
    CFRelease(cpath);

	return true;
}

#endif

bool watchDirectory(const char* path, const std::function<void (const char* name)>& callback)
{
#if defined(__linux__)
	return watchDirectoryInotify(path, callback);
#elif defined(__APPLE__)
	return watchDirectoryFSEvent(path, callback);
#else
	return false;
#endif
}
#endif
