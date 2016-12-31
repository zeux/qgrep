#include "common.hpp"
#include "compression.hpp"

#include "lz4.h"
#include "lz4hc.h"

std::pair<std::unique_ptr<char[]>, size_t> compress(const void* data, size_t dataSize)
{
	if (dataSize == 0) return std::make_pair(std::unique_ptr<char[]>(), 0);

	int compressionLevel = LZ4HC_DEFAULT_CLEVEL;
	int csizeBound = LZ4_compressBound(dataSize);

	std::unique_ptr<char[]> cdata(new char[csizeBound]);
	
	int csize = LZ4_compress_HC(static_cast<const char*>(data), cdata.get(), dataSize, csizeBound, compressionLevel);
	assert(csize >= 0 && csize <= csizeBound);

	return std::make_pair(std::move(cdata), csize);
}

void decompress(void* dest, size_t destSize, const void* source, size_t sourceSize)
{
	if (sourceSize == 0 && destSize == 0) return;

	int result = LZ4_decompress_fast(static_cast<const char*>(source), static_cast<char*>(dest), destSize);
	assert(result >= 0);
	assert(static_cast<size_t>(result) == sourceSize);
}