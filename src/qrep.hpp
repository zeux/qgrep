#ifndef QREP_HPP
#define QREP_HPP

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

void error(const char* message, ...);
void fatal(const char* message, ...);

enum SearchOptions
{
	SO_IGNORECASE = 1 << 0,
	SO_VISUALSTUDIO = 1 << 1
};
	
void initProject(const char* file, const char* path);
void buildProject(const char* file);
void searchProject(const char* file, const char* string, unsigned int options);

#endif
