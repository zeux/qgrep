#include "common.hpp"
#include "update.hpp"

#include "output.hpp"
#include "build.hpp"
#include "format.hpp"
#include "fileutil.hpp"
#include "filestream.hpp"
#include "project.hpp"
#include "files.hpp"
#include "compression.hpp"

#include <memory>
#include <vector>
#include <string>
#include <chrono>

#include <string.h>

struct UpdateStatistics
{
	unsigned int filesAdded;
	unsigned int filesRemoved;
	unsigned int filesChanged;
	unsigned int chunksPreserved;
};

struct UpdateFileIterator
{
	const FileInfo& operator*() const
	{
		assert(index < files.size());
		return files[index];
	}

	const FileInfo* operator->() const
	{
		return &**this;
	}

	operator bool() const
	{
		return index < files.size();
	}

	UpdateFileIterator& operator++()
	{
		assert(index < files.size());
		index++;
		return *this;
	}

	UpdateFileIterator& operator+=(unsigned int diff)
	{
		assert(index + diff <= files.size());
		index += diff;
		return *this;
	}

	const std::vector<FileInfo>& files;
	size_t index;
};

static int comparePath(const FileInfo& info, const DataChunkFileHeader& file, const char* data)
{
	return info.path.compare(0, info.path.length(), data + file.nameOffset, file.nameLength);
}

static bool isFileCurrent(const FileInfo& info, const DataChunkFileHeader& file, const char* data)
{
	assert(comparePath(info, file, data) == 0);
	return info.lastWriteTime == file.timeStamp && info.fileSize == file.fileSize;
}

static bool isChunkCurrent(UpdateFileIterator& fileit, const DataChunkHeader& chunk, const DataChunkFileHeader* files, const char* data, bool firstFileIsSuffix)
{
	// if first file in the chunk is not the first file part, then the chunk is current iff we added this file before and the rest is current
	// so we can just start comparison 1 entry before
	size_t back = firstFileIsSuffix ? 1 : 0;
	if (fileit.index < back || fileit.index - back + chunk.fileCount > fileit.files.size()) return false;

	for (size_t i = 0; i < chunk.fileCount; ++i)
	{
		const DataChunkFileHeader& f = files[i];
		const FileInfo& info = fileit.files[fileit.index - back + i];

		if (comparePath(info, f, data) != 0 || !isFileCurrent(info, f, data))
			return false;
	}

	return true;
}

static void processChunkData(Output* output, Builder* builder, UpdateFileIterator& fileit, UpdateStatistics& stats,
	const DataChunkHeader& chunk, const char* data, std::unique_ptr<char[]>& compressed, std::unique_ptr<char[]>& index)
{
	const DataChunkFileHeader* files = reinterpret_cast<const DataChunkFileHeader*>(data);

	// if chunk is fully up-to-date, we can try adding it directly and skipping chunk recompression
	assert(chunk.fileCount > 0);

	bool firstFileIsSuffix = files[0].startLine > 0;

	if (isChunkCurrent(fileit, chunk, files, data, firstFileIsSuffix) && builder->appendChunk(chunk, compressed, index, firstFileIsSuffix))
	{
		fileit += chunk.fileCount - firstFileIsSuffix;
		stats.chunksPreserved++;
		return;
	}

	// as a special case, first file in the chunk can be a part of an existing file
	if (files[0].startLine > 0 && fileit.index > 0)
	{
		// in this case, if the file is current then we only added the part before this in the previous chunk processing, so just add the next part
		const DataChunkFileHeader& f = files[0];
		const FileInfo* prev = &fileit.files[fileit.index - 1];

		if (comparePath(*prev, f, data) == 0 && isFileCurrent(*prev, f, data))
		{
			builder->appendFilePart(prev->path.c_str(), f.startLine, data + f.dataOffset, f.dataSize, prev->lastWriteTime, prev->fileSize);
		}
	}

	for (size_t i = 0; i < chunk.fileCount; ++i)
	{
		const DataChunkFileHeader& f = files[i];

		// add all files before the file
		while (fileit && comparePath(*fileit, f, data) < 0)
		{
			builder->appendFile(fileit->path.c_str(), fileit->lastWriteTime, fileit->fileSize);
			++fileit;
			stats.filesAdded++;
		}

		// check if file exists
		if (fileit && comparePath(*fileit, f, data) == 0)
		{
			// check if we can reuse the data from qgrep db
			if (isFileCurrent(*fileit, f, data))
				builder->appendFilePart(fileit->path.c_str(), f.startLine, data + f.dataOffset, f.dataSize, fileit->lastWriteTime, fileit->fileSize);
			else
			{
				builder->appendFile(fileit->path.c_str(), fileit->lastWriteTime, fileit->fileSize);
				stats.filesChanged++;
			}

			++fileit;
		}
		else if (f.startLine == 0)
		{
			stats.filesRemoved++;
		}
	}
}

static bool processFile(Output* output, Builder* builder, UpdateFileIterator& fileit, UpdateStatistics& stats, const char* path)
{
	FileStream in(path, "rb");
	if (!in) return true;

	DataFileHeader header;
	if (!read(in, header) || memcmp(header.magic, kDataFileHeaderMagic, strlen(kDataFileHeaderMagic)) != 0)
	{
		output->error("Error reading data file %s: malformed header\n", path);
		return false;
	}

	DataChunkHeader chunk;

	while (read(in, chunk))
	{
		std::unique_ptr<char[]> index(new (std::nothrow) char[chunk.indexSize]);
		std::unique_ptr<char[]> data(new (std::nothrow) char[chunk.compressedSize + chunk.uncompressedSize]);

		if (!index || !data || !read(in, index.get(), chunk.indexSize) || !read(in, data.get(), chunk.compressedSize))
		{
			output->error("Error reading data file %s: malformed chunk\n", path);
			return false;
		}

		char* uncompressed = data.get() + chunk.compressedSize;

		decompress(uncompressed, chunk.uncompressedSize, data.get(), chunk.compressedSize);
		processChunkData(output, builder, fileit, stats, chunk, uncompressed, data, index);
	}

	return true;
}

static void printStatistics(Output* output, const UpdateStatistics& stats, unsigned int totalChunks, double time)
{
	if (stats.filesAdded) output->print("+%d ", stats.filesAdded);
	if (stats.filesRemoved) output->print("-%d ", stats.filesRemoved);
	if (stats.filesChanged) output->print("*%d ", stats.filesChanged);

	output->print("%s; %d/%d chunks updated in %.2f sec\n",
		(stats.filesAdded || stats.filesRemoved || stats.filesChanged) ? "files" : "No changes",
		totalChunks - stats.chunksPreserved, totalChunks, time);
}

void updateProject(Output* output, const char* path)
{
	auto start = std::chrono::high_resolution_clock::now();

    output->print("Updating %s:\n", path);
	output->print("Scanning project...\r");

	std::vector<FileInfo> files;
	if (!getProjectFiles(output, path, files))
	{
		return;
	}

	buildFiles(output, path, files);
	
	std::string targetPath = replaceExtension(path, ".qgd");
	std::string tempPath = targetPath + "_";

	UpdateStatistics stats = {};
	unsigned int totalChunks = 0;

	{
		std::unique_ptr<Builder> builder(createBuilder(output, tempPath.c_str(), files.size()));
		if (!builder) return;

		UpdateFileIterator fileit = {files, 0};

		// update contents using existing database (if any)
		if (!processFile(output, builder.get(), fileit, stats, targetPath.c_str())) return;

		// update all unprocessed files
		while (fileit)
		{
			builder->appendFile(fileit->path.c_str(), fileit->lastWriteTime, fileit->fileSize);
			++fileit;
			stats.filesAdded++;
		}

		totalChunks = builder->finish();
	}

	output->print("\n");

	auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start);

	printStatistics(output, stats, totalChunks, time.count() / 1e3);
	
	if (!renameFile(tempPath.c_str(), targetPath.c_str()))
	{
		output->error("Error saving data file %s\n", targetPath.c_str());
		return;
	}
}
