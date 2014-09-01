#include "common.hpp"
#include "compression.hpp"

#include "lz4/lz4.h"
#include "lz4/lz4hc.h"

std::vector<char> compress(const std::vector<char>& data)
{
	if (data.empty()) return std::vector<char>();

	std::vector<char> cdata(LZ4_compressBound(data.size()));
	
	int csize = LZ4_compressHC(const_cast<char*>(&data[0]), &cdata[0], data.size());
	assert(csize >= 0 && static_cast<size_t>(csize) <= cdata.size());

	cdata.resize(csize);

	return cdata;
}

void decompress(void* dest, size_t destSize, const void* source, size_t sourceSize)
{
	if (sourceSize == 0 && destSize == 0) return;

	int result = LZ4_decompress_fast(static_cast<const char*>(source), static_cast<char*>(dest), destSize);
	assert(result >= 0);
	assert(static_cast<size_t>(result) == sourceSize);
}