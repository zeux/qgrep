#pragma once

#include <stdio.h>

class FileStream
{
public:
	FileStream();
	FileStream(const char* path, const char* mode);
	~FileStream();

	bool open(const char* path, const char* mode);

	operator bool() const;

	void skip(size_t offset);
	size_t read(void* data, size_t size);
	size_t write(const void* data, size_t size);

private:
	void* file;
};

inline bool read(FileStream& in, void* data, size_t size)
{
	return in.read(data, size) == size;
}

template <typename T> inline bool read(FileStream& in, T& value)
{
	return in.read(&value, sizeof(T)) == sizeof(T);
}
