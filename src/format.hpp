#pragma once

#include <stdint.h>

const char kFileFileHeaderMagic[] = "QGF0";

struct FileFileHeader
{
	char magic[4];

	uint32_t fileCount;
	uint32_t compressedSize;
	uint32_t uncompressedSize;

	uint32_t nameBufferOffset;
	uint32_t pathBufferOffset;
};

struct FileFileEntry
{
	uint32_t nameOffset;
	uint32_t pathOffset;
};

const char kDataFileHeaderMagic[] = "QGD0";

struct DataFileHeader
{
	char magic[4];
};

struct DataChunkHeader
{
	uint32_t fileCount;
	uint32_t compressedSize;
	uint32_t uncompressedSize;
};

struct DataChunkFileHeader
{
	uint32_t nameOffset;
	uint32_t nameLength;
	uint32_t dataOffset;
	uint32_t dataSize;

	uint32_t startLine;
	uint32_t reserved;

	uint64_t fileSize;
	uint64_t timeStamp;
};
