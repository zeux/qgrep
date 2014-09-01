#include "common.hpp"
#include "compression.hpp"

#include "lz4/lz4.h"
#include "lz4/lz4hc.h"

std::pair<std::unique_ptr<char[]>, size_t> compress(const void* data, size_t dataSize)
{
	if (dataSize == 0) return std::make_pair(std::unique_ptr<char[]>(), 0);

	std::unique_ptr<char[]> cdata(new char[LZ4_compressBound(dataSize)]);
	
	int csize = LZ4_compressHC(const_cast<char*>(static_cast<const char*>(data)), cdata.get(), dataSize);
	assert(csize >= 0 && static_cast<size_t>(csize) <= LZ4_compressBound(dataSize));

	return std::make_pair(std::move(cdata), csize);
}

void decompress(void* dest, size_t destSize, const void* source, size_t sourceSize)
{
	if (sourceSize == 0 && destSize == 0) return;

	int result = LZ4_decompress_fast(static_cast<const char*>(source), static_cast<char*>(dest), destSize);
	assert(result >= 0);
	assert(static_cast<size_t>(result) == sourceSize);
}