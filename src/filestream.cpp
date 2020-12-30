// This file is part of qgrep and is distributed under the MIT license, see LICENSE.md
#include "common.hpp"
#include "filestream.hpp"

#include "fileutil.hpp"

#include <stdio.h>

#ifdef _WIN32
#   define fseeko _fseeki64
#endif

FileStream::FileStream(): file(0)
{
}

FileStream::FileStream(const char* path, const char* mode): file(0)
{
    open(path, mode);
}

FileStream::~FileStream()
{
    if (file) fclose(static_cast<FILE*>(file));
}

bool FileStream::open(const char* path, const char* mode)
{
    assert(!file);
    file = openFile(path, mode);
    return file != nullptr;
}

FileStream::operator bool() const
{
    return file && ferror(static_cast<FILE*>(file)) == 0;
}

void FileStream::skip(size_t offset)
{
    fseeko(static_cast<FILE*>(file), offset, SEEK_CUR);
}

size_t FileStream::read(void* data, size_t size)
{
    return fread(data, 1, size, static_cast<FILE*>(file));
}

size_t FileStream::write(const void* data, size_t size)
{
    return fwrite(data, 1, size, static_cast<FILE*>(file));
}
