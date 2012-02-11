#include "qrep.hpp"

#include "fileutil.hpp"

#include <fstream>
#include <vector>
#include <numeric>
#include <cassert>
#include <string>

#include "lz4/lz4.h"
#include "re2/re2.h"

class Builder
{
public:
	static const size_t kChunkSize = 1*1024*1024;

	struct Statistics
	{
		size_t fileCount;
		uint64_t fileSize;
		uint64_t resultSize;
	};

	Builder()
	{
		statistics = Statistics();
	}

	~Builder()
	{
		flush();
	}

	bool start(const char* path)
	{
		outData.open(path, std::ios::out | std::ios::binary);
		if (!outData) return false;

		FileHeader header;
		memcpy(header.magic, kFileHeaderMagic, sizeof(header.magic));

		outData.write(reinterpret_cast<char*>(&header), sizeof(header));

		return true;
	}

	bool appendFile(const char* path)
	{
		if (currentChunk.totalSize > kChunkSize) flushChunk();

		std::ifstream in(path, std::ios::in | std::ios::binary);
		if (!in) return false;
	
		File file = {path};

		while (!in.eof())
		{
			char buffer[65536];
			in.read(buffer, sizeof(buffer));

			file.contents.insert(file.contents.end(), buffer, buffer + in.gcount());
		}

		currentChunk.files.push_back(file);
		currentChunk.totalSize += file.contents.size();

		return true;
	}

	void flush()
	{
		flushChunk();
	}

	const Statistics& getStatistics() const
	{
		return statistics;
	}

private:
	struct File
	{
		std::string name;
		std::vector<char> contents;
	};

	struct Chunk
	{
		std::vector<File> files;
		size_t totalSize;

		Chunk(): totalSize(0)
		{
		}
	};

	Chunk currentChunk;
	std::ofstream outData;
	Statistics statistics;

	static std::vector<char> compressData(const std::vector<char>& data)
	{
		// LZ4 worst-case size calculation :-/
		std::vector<char> cdata(data.size() + data.size() / 100 + 16);

		int csize = LZ4_compress(&data[0], &cdata[0], data.size());
		assert(csize <= cdata.size());

		cdata.resize(csize);

		return cdata;
	}

	void flushChunk()
	{
		if (currentChunk.files.empty()) return;

		std::vector<char> data = prepareChunkData(currentChunk);
		writeChunk(currentChunk, data);

		currentChunk = Chunk();
	}

	size_t getChunkNameTotalSize(const Chunk& chunk)
	{
		size_t result = 0;

		for (size_t i = 0; i < chunk.files.size(); ++i)
			result += chunk.files[i].name.size();

		return result;
	}

	size_t getChunkDataTotalSize(const Chunk& chunk)
	{
		size_t result = 0;

		for (size_t i = 0; i < chunk.files.size(); ++i)
			result += chunk.files[i].contents.size();

		return result;
	}

	std::vector<char> prepareChunkData(const Chunk& chunk)
	{
		size_t headerSize = sizeof(ChunkFileHeader) * chunk.files.size();
		size_t nameSize = getChunkNameTotalSize(chunk);
		size_t dataSize = getChunkDataTotalSize(chunk);
		size_t totalSize = headerSize + nameSize + dataSize;

		std::vector<char> data(totalSize);

		size_t nameOffset = headerSize;
		size_t dataOffset = headerSize + nameSize;

		for (size_t i = 0; i < chunk.files.size(); ++i)
		{
			const File& f = chunk.files[i];

			std::copy(f.name.begin(), f.name.end(), data.begin() + nameOffset);
			std::copy(f.contents.begin(), f.contents.end(), data.begin() + dataOffset);

			ChunkFileHeader& h = reinterpret_cast<ChunkFileHeader*>(&data[0])[i];

			h.nameOffset = nameOffset;
			h.nameLength = f.name.size();
			h.dataOffset = dataOffset;
			h.dataSize = f.contents.size();

			nameOffset += f.name.size();
			dataOffset += f.contents.size();
		}

		assert(nameOffset == headerSize + nameSize && dataOffset == totalSize);

		return data;
	}

	void writeChunk(const Chunk& chunk, const std::vector<char>& data)
	{
		std::vector<char> cdata = compressData(data);

		ChunkHeader header = {};
		header.fileCount = chunk.files.size();
		header.uncompressedSize = data.size();
		header.compressedSize = cdata.size();

		outData.write(reinterpret_cast<char*>(&header), sizeof(header));
		outData.write(&cdata[0], cdata.size());

		statistics.fileCount += chunk.files.size();
		statistics.fileSize += data.size();
		statistics.resultSize += cdata.size();
	}
};

static std::string trim(const std::string& s)
{
	const char* pattern = " \t";

	std::string::size_type b = s.find_first_not_of(pattern);
	std::string::size_type e = s.find_last_not_of(pattern);

	return (b == std::string::npos || e == std::string::npos) ? "" : s.substr(b, e + 1);
}

static bool extractSuffix(const std::string& str, const char* prefix, std::string& suffix)
{
	size_t length = strlen(prefix);

	if (str.compare(0, length, prefix) == 0 && str.length() > length && isspace(str[length]))
	{
		suffix = trim(str.substr(length));
		return true;
	}

	return false;
}

static bool parseInput(const char* file, std::string& path, std::vector<std::string>& include, std::vector<std::string>& exclude, std::vector<std::string>& files)
{
	std::ifstream in(file);
	if (!in) return false;

	std::string line;
	std::string suffix;

	while (std::getline(in, line))
	{
		// remove comment
		std::string::size_type shp = line.find('#');
		if (shp != std::string::npos) line.erase(line.begin() + shp, line.end());

		// parse lines
		if (extractSuffix(line, "path", suffix))
			path = suffix;
		else if (extractSuffix(line, "include", suffix))
			include.push_back(suffix);
		else if (extractSuffix(line, "exclude", suffix))
			exclude.push_back(suffix);
		else
		{
			std::string file = trim(line);
			if (!file.empty()) files.push_back(file);
		}
	}

	return true;
}

static RE2* constructOrRE(const std::vector<std::string>& list)
{
	if (list.empty()) return 0;
	
	std::string re = "(" + list[0] + ")";

	for (size_t i = 1; i < list.size(); ++i)
		re += "|(" + list[i] + ")";

	RE2::Options opts;
	opts.set_case_sensitive(false);

	RE2* r = new RE2(re, opts);

	if (!r->ok()) fatal("Error parsing regexp %s: %s\n", re.c_str(), r->error().c_str());

	return r;
}

static void printStatistics(const Builder::Statistics& s)
{
	static uint64_t lastResultSize = 0;
	if (lastResultSize == s.resultSize) return;
	lastResultSize = s.resultSize;

	static int progress = 0;
	char progressBar[] = "     ";
	progressBar[progress++ % (sizeof(progressBar) - 1)] = '.';

	printf("[%s] %d files, %d Mb in, %d Mb out\r", progressBar, s.fileCount, (int)(s.fileSize / 1024 / 1024), (int)(s.resultSize / 1024 / 1024));
	fflush(stdout);
}

struct BuilderContext
{
	Builder builder;
	RE2* include;
	RE2* exclude;
};

static void builderAppend(Builder& builder, const char* path)
{
	if (!builder.appendFile(path))
	{
		error("Error reading file %s\n", path);
	}

	printStatistics(builder.getStatistics());
}

static void traverseBuilderAppend(void* context, const char* path)
{
	BuilderContext& c = *static_cast<BuilderContext*>(context);

	if (c.include && !RE2::PartialMatch(path, *c.include))
		return;

	if (c.exclude && RE2::PartialMatch(path, *c.exclude))
		return;

	builderAppend(c.builder, path);
}

static std::string replaceExtension(const char* path, const char* ext)
{
	const char* dot = strrchr(path, '.');

	return dot ? std::string(path, dot) + ext : std::string(path) + ext;
}

void build(const char* file)
{
	std::string path;
	std::vector<std::string> includeSet, excludeSet, fileSet;

	if (!parseInput(file, path, includeSet, excludeSet, fileSet))
		fatal("Error opening rule file %s for reading\n", file);

	BuilderContext bc;
	bc.include = constructOrRE(includeSet);
	bc.exclude = constructOrRE(excludeSet);

	std::string data = replaceExtension(file, ".qgd");

	if (!bc.builder.start(data.c_str()))
		fatal("Error opening data file %s for writing\n", data.c_str());

	if (!path.empty())
		traverseDirectory(path.c_str(), traverseBuilderAppend, &bc);

	for (size_t i = 0; i < fileSet.size(); ++i)
		builderAppend(bc.builder, fileSet[i].c_str());

	bc.builder.flush();
	printStatistics(bc.builder.getStatistics());
}
