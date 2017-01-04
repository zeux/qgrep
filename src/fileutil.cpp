#include "common.hpp"
#include "fileutil.hpp"

#include <string.h>

static bool isSeparator(char ch)
{
	return ch == '/' || ch == '\\';
}

void createPath(const char* path)
{
	std::string p = path;

	for (size_t i = 0; i < p.size(); ++i)
	{
		if (isSeparator(p[i]))
		{
			char ch = p[i];

			p[i] = 0;
			createDirectory(p.c_str());

			p[i] = ch;
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

bool traverseFileNeeded(const char* name)
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

void joinPaths(std::string& buf, const char* lhs, const char* rhs)
{
	buf = lhs;
	if (!buf.empty() && !isSeparator(buf.back()) && !isSeparator(*rhs)) buf += "/";
	buf += rhs;
}

static void appendPathComponent(std::string& buf, const char* begin, const char* end)
{
    size_t length = end - begin;

    if (length == 2 && begin[0] == '.' && begin[1] == '.')
    {
        size_t lpos = buf.find_last_of('/');

        if (lpos != std::string::npos)
            buf.erase(buf.begin() + (lpos == 0 ? 1 : lpos), buf.end());
    }
    else if (length > 0 && !(length == 1 && begin[0] == '.'))
    {
        if (!buf.empty() && !isSeparator(buf.back()))
            buf.push_back('/');

        buf.insert(buf.end(), begin, end);
    }
}

static void appendPathComponents(std::string& buf, const char* path)
{
    const char* begin = path;

    for (const char* end = path; ; ++end)
    {
        if (isSeparator(*end) || !*end)
        {
            appendPathComponent(buf, begin, end);

            begin = end + 1;
        }

        if (!*end) break;
    }
}

static bool isUNCPath(const char* path)
{
    return (path[0] == '\\' && path[1] == '\\');
}

static bool isDriveLetter(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

static bool isDrivePath(const char* path)
{
    return isDriveLetter(path[0]) && path[1] == ':' && (path[2] == 0 || isSeparator(path[2]));
}

static void appendPath(std::string& buf, const char* path)
{
    // handle absolute paths
    if (isUNCPath(path))
    {
        // UNC path; keep backslashes (all other backslashes are replaced with forward slashes)
		buf.assign("\\\\");
        path += 2;
    }
    else if (isDrivePath(path))
    {
        // Windows drive path
		buf.assign(path, 2);
        path += 2;
    }
    else if (isSeparator(path[0]))
    {
        if (isUNCPath(buf.c_str()) || isDrivePath(buf.c_str()))
        {
            // go to UNC or drive root
            size_t pos = buf.find('/');

            if (pos != std::string::npos)
				buf.erase(buf.begin() + pos, buf.end());
        }
        else
        {
            // go to FS root
			buf.assign("/");
        }
    }

    // handle other path components as relative
    appendPathComponents(buf, path);
}

std::string normalizePath(const char* base, const char* path)
{
	std::string result;
	result.reserve(strlen(base) + 1 + strlen(path));

    appendPath(result, base);
    appendPath(result, path);

    return result;
}
