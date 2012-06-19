#pragma once

#include <istream>

inline bool read(std::istream& in, void* data, size_t size)
{
	in.read(static_cast<char*>(data), size);
	return static_cast<size_t>(in.gcount()) == size;
}

template <typename T> inline bool read(std::istream& in, T& value)
{
	return read(in, &value, sizeof(T));
}
