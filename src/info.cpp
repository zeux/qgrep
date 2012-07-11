#include "common.hpp"
#include "info.hpp"

#include "output.hpp"
#include "format.hpp"
#include "stringutil.hpp"
#include "fileutil.hpp"
#include "filestream.hpp"
#include "compression.hpp"

#include <memory>
#include <string>
#include <type_traits>

template <typename T> struct Statistics
{
	unsigned int count;
	T min, max;

	typename std::conditional<
		std::is_floating_point<T>::value,
		double,
		typename std::conditional<
			std::is_signed<T>::value,
			long long,
			unsigned long long>::type>::type total;

	Statistics(): count(0), min(0), max(0), total(0)
	{
	}

	void update(T value)
	{
		min = (count == 0) ? value : std::min(min, value);
		max = (count == 0) ? value : std::max(max, value);
		total += value;
		count++;
	}

	double average() const
	{
		return count == 0 ? 0 : static_cast<double>(total) / static_cast<double>(count);
	}
};

struct ProjectInfo
{
	unsigned int chunkCount;

	Statistics<unsigned int> chunkSizeExceptLast;
	Statistics<unsigned int> chunkSize;

	Statistics<unsigned int> chunkCompressedSizeExceptLast;
	Statistics<unsigned int> chunkCompressedSize;
	Statistics<double> chunkCompressionRatio;

	unsigned int indexChunkCount;
	unsigned long long indexTotalSize;
	Statistics<unsigned int> indexHashIterations;
	Statistics<double> indexFilled;

	unsigned long long lineCount;
	unsigned int lineMaxSize;
	std::string lineMaxSizeFile;

	unsigned int fileCount;
	unsigned int filePartCount;
	unsigned long long fileTotalSize;

	std::string lastFile;
	uint64_t lastFileSize;
	uint64_t lastFileTimeStamp;
	unsigned int lastFileLine;
};

static std::pair<size_t, size_t> getLineStatistics(const char* data, size_t size)
{
	size_t count = 0;
	size_t maxLength = 0;

	for (size_t last = 0; last < size; )
	{
		const char* next = static_cast<const char*>(memchr(data + last, '\n', size - last));
		if (!next) next = data + size;

		count++;
		maxLength = std::max(maxLength, (next - data) - last);

		last = (next - data) + 1;
	}

	return std::make_pair(count, maxLength);
}

static void processFilePart(Output* output, ProjectInfo& info, const char* path, uint64_t fileSize, uint64_t timeStamp, const char* data, size_t size, uint32_t startLine)
{
	if (info.lastFile > path)
		output->error("Error: file ordering mismatch (%s is before %s)\n", info.lastFile.c_str(), path);

	if (info.lastFile == path && (info.lastFileSize != fileSize || info.lastFileTimeStamp != timeStamp))
		output->error("Error: file metadata mismatch between chunks (%s: %llx %llx != %llx %llx)\n", path, info.lastFileSize, info.lastFileTimeStamp, fileSize, timeStamp);

	if (info.lastFile == path && info.lastFileLine != startLine)
		output->error("Error: file line data mismatch between chunks (%s: %d != %d)\n", path, info.lastFileLine, startLine);

	// update line info
	std::pair<size_t, size_t> ls = getLineStatistics(data, size);

	info.lineCount += ls.first;
	info.lineMaxSize = std::max<unsigned int>(info.lineMaxSize, ls.second);

	if (info.lineMaxSize == ls.second) info.lineMaxSizeFile = path;

	// update file info
	info.fileCount += (info.lastFile != path);
	info.filePartCount++;
	info.fileTotalSize += size;

	info.lastFile = path;
	info.lastFileSize = fileSize;
	info.lastFileTimeStamp = timeStamp;
	info.lastFileLine = startLine + ls.first;
}

inline unsigned int popcount(unsigned char v)
{
	return
		((v >> 0) & 1) +
		((v >> 1) & 1) +
		((v >> 2) & 1) +
		((v >> 3) & 1) +
		((v >> 4) & 1) +
		((v >> 5) & 1) +
		((v >> 6) & 1) +
		((v >> 7) & 1);
}

static void processChunkIndex(Output* output, ProjectInfo& info, const DataChunkHeader& header, const char* data)
{
	info.indexHashIterations.update(header.indexHashIterations);

	unsigned int filled = 0;

	for (size_t i = 0; i < header.indexSize; ++i)
		filled += popcount(static_cast<unsigned char>(data[i]));

	double filledRatio = static_cast<double>(filled) / static_cast<double>(header.indexSize * 8);

	info.indexFilled.update(filledRatio);

	info.indexTotalSize += header.indexSize;

	info.indexChunkCount++;
}

static void processChunkData(Output* output, ProjectInfo& info, const DataChunkHeader& header, const char* data)
{
	const DataChunkFileHeader* files = reinterpret_cast<const DataChunkFileHeader*>(data);

	// update chunk size stats
	info.chunkSizeExceptLast = info.chunkSize;
	info.chunkCompressedSizeExceptLast = info.chunkCompressedSize;
	info.chunkCompressionRatio.update(static_cast<double>(header.uncompressedSize) / static_cast<double>(header.compressedSize));

	info.chunkSize.update(header.uncompressedSize);
	info.chunkCompressedSize.update(header.compressedSize);

	info.chunkCount++;

	// update file stats
	for (size_t i = 0; i < header.fileCount; ++i)
	{
		const DataChunkFileHeader& f = files[i];

		std::string path(data + f.nameOffset, f.nameLength);

		processFilePart(output, info, path.c_str(), f.fileSize, f.timeStamp, data + f.dataOffset, f.dataSize, f.startLine);
	}
}

static bool processFile(Output* output, ProjectInfo& info, const char* path)
{
	FileStream in(path, "rb");
	if (!in)
	{
		output->error("Error reading data file %s\n", path);
		return false;
	}

	DataFileHeader header;
	if (!read(in, header) || memcmp(header.magic, kDataFileHeaderMagic, strlen(kDataFileHeaderMagic)) != 0)
	{
		output->error("Error reading data file %s: malformed header\n", path);
		return false;
	}

	DataChunkHeader chunk;

	while (read(in, chunk))
	{
		if (chunk.indexSize)
		{
			std::unique_ptr<char[]> index(new (std::nothrow) char[chunk.indexSize]);

			if (!index || !read(in, index.get(), chunk.indexSize))
			{
				output->error("Error reading data file %s: malformed chunk\n", path);
				return false;
			}

			processChunkIndex(output, info, chunk, index.get());
		}

		std::unique_ptr<char[]> data(new (std::nothrow) char[chunk.compressedSize + chunk.uncompressedSize]);

		if (!data || !read(in, data.get(), chunk.compressedSize))
		{
			output->error("Error reading data file %s: malformed chunk\n", path);
			return false;
		}

		char* uncompressed = data.get() + chunk.compressedSize;

		decompress(uncompressed, chunk.uncompressedSize, data.get(), chunk.compressedSize);
		processChunkData(output, info, chunk, uncompressed);
	}

	return true;
}

static std::string formatInteger(unsigned long long value, bool pad0 = false)
{
	if (value < 1000)
	{
		char buf[32];
		sprintf(buf, pad0 ? "%03lld" : "%lld", value);
		return buf;
	}
	else
	{
		return formatInteger(value / 1000, pad0) + " " + formatInteger(value % 1000, true);
	}
}

static std::string lastChunkSize(unsigned long long sizeExceptLast, unsigned long long size)
{
	if (sizeExceptLast == size)
		return "";
	else
	{
		char buf[256];
		sprintf(buf, "; last chunk %s bytes", formatInteger(size).c_str());
		return buf;
	}
}

void printProjectInfo(Output* output, const char* path)
{
    output->print("Project %s:\n", path);
	output->print("Reading project...\r");

	ProjectInfo info = {};

	std::string dataPath = replaceExtension(path, ".qgd");

	if (processFile(output, info, dataPath.c_str()))
	{
		if (info.chunkCount <= 1)
		{
			info.chunkSizeExceptLast = info.chunkSize;
			info.chunkCompressedSizeExceptLast = info.chunkCompressedSize;
		}

	#define FI(v) formatInteger(v).c_str()

		output->print("Files: %s (%s file parts)\n", FI(info.fileCount), FI(info.filePartCount));
		output->print("File data: %s bytes\n", FI(info.fileTotalSize));
		output->print("Lines: %s (longest line: %s bytes in %s)\n", FI(info.lineCount), FI(info.lineMaxSize), info.lineMaxSizeFile.c_str());

		output->print("Chunks (data): %s (%s bytes, [%s..%s] (avg %s) bytes per chunk%s)\n",
			FI(info.chunkCount), FI(info.chunkSize.total), FI(info.chunkSizeExceptLast.min), FI(info.chunkSize.max),
			FI(static_cast<unsigned long long>(info.chunkSize.average())),
			lastChunkSize(info.chunkSizeExceptLast.min, info.chunkSize.min).c_str());

		output->print("Chunks (compressed): %s bytes, [%s..%s] (avg %s) bytes per chunk%s\n",
			FI(info.chunkCompressedSize.total), FI(info.chunkCompressedSizeExceptLast.min), FI(info.chunkCompressedSize.max),
			FI(static_cast<unsigned long long>(info.chunkCompressedSize.average())),
			lastChunkSize(info.chunkCompressedSizeExceptLast.min, info.chunkCompressedSize.min).c_str());

		output->print("Chunk compression ratio: %.2f ([%.2f..%.2f] (avg %.2f) per chunk)\n",
			info.chunkCompressedSize.total == 0 ? 1.0 : static_cast<double>(info.chunkSize.total) / static_cast<double>(info.chunkCompressedSize.total),
			info.chunkCompressionRatio.min, info.chunkCompressionRatio.max, info.chunkCompressionRatio.average());

		output->print("Index: %s chunks (%s bytes, hash iterations [%d..%d] (avg %.1f), filled ratio [%.1f%%..%.1f%%] (avg %.1f%%))\n",
			FI(info.indexChunkCount), FI(info.indexTotalSize),
			info.indexHashIterations.min, info.indexHashIterations.max, info.indexHashIterations.average(),
			info.indexFilled.min * 100, info.indexFilled.max * 100, info.indexFilled.average() * 100);

	#undef FI
	}
}
