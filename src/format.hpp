#ifndef FORMAT_HPP
#define FORMAT_HPP

#include <stdint.h>

const char kFileHeaderMagic[] = "QGD0";

struct FileHeader
{
	char magic[4];
};

struct ChunkHeader
{
	uint32_t fileCount;
	uint32_t compressedSize;
	uint32_t uncompressedSize;
};

struct ChunkFileHeader
{
	uint32_t nameOffset;
	uint32_t nameLength;
	uint32_t dataOffset;
	uint32_t dataSize;
};

#endif
