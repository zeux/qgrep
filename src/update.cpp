#include "update.hpp"

#include "output.hpp"
#include "build.hpp"
#include "format.hpp"
#include "fileutil.hpp"
#include "project.hpp"
#include "files.hpp"

#include <fstream>
#include <memory>
#include <cassert>
#include <vector>
#include <string>

#include "lz4/lz4.h"

inline bool read(std::istream& in, void* data, size_t size)
{
	in.read(static_cast<char*>(data), size);
	return in.gcount() == size;
}

template <typename T> inline bool read(std::istream& in, T& value)
{
	return read(in, &value, sizeof(T));
}

char* safeAlloc(size_t size)
{
	try
	{
		return new char[size];
	}
	catch (const std::bad_alloc&)
	{
		return nullptr;
	}
}

class FileDataIterator
{
public:
	FileDataIterator(const char* path): file(0)
	{
		in.open(path, std::ios::in | std::ios::binary);

		DataFileHeader header;
		if (!read(in, header) || memcmp(header.magic, kDataFileHeaderMagic, strlen(kDataFileHeaderMagic)) != 0)
		{
			in.close();
		}

		chunk.fileCount = 0;
		
		moveNext();
	}

	operator bool() const
	{
		return file < chunk.fileCount;
	}

	const DataChunkFileHeader& getHeader() const
	{
		assert(*this);
		return reinterpret_cast<const DataChunkFileHeader*>(data.get())[file];
	}

	std::string getPath() const
	{
		const DataChunkFileHeader& header = getHeader();
		return std::string(data.get() + header.nameOffset, header.nameLength);
	}

	const char* getData() const
	{
		const DataChunkFileHeader& header = getHeader();
		return data.get() + header.dataOffset;
	}

	void moveNext()
	{
		if (file + 1 < chunk.fileCount)
			file++;
		else
		{
			if (read(in, chunk))
			{
				in.seekg(chunk.indexSize, std::ios::cur);

				std::unique_ptr<char[]> compressed(safeAlloc(chunk.compressedSize));
				data.reset(safeAlloc(chunk.uncompressedSize));

				if (data && compressed && read(in, compressed.get(), chunk.compressedSize))
				{
					LZ4_uncompress(compressed.get(), data.get(), chunk.uncompressedSize);
					file = 0;
					return;
				}
			}

			file = 0;
			chunk.fileCount = 0;
			in.close();
		}
	}

private:
	std::ifstream in;
	DataChunkHeader chunk;
	std::unique_ptr<char[]> data;
	unsigned int file;
};

std::string getCurrentFileContents(FileDataIterator& it)
{
	std::string path = it.getPath();
	std::string contents;

	do
	{
		contents.insert(contents.end(), it.getData(), it.getData() + it.getHeader().dataSize);
		it.moveNext();
	}
	while (it && it.getPath() == path);

	return contents;
}

void updateProject(Output* output, const char* path)
{
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

	{
		FileDataIterator current(targetPath.c_str());

		std::unique_ptr<Builder> builder(createBuilder(output, tempPath.c_str(), files.size()));
		if (!builder) return;

		for (auto& f: files)
		{
			// skip to the file, if any
			while (current && current.getPath() < f.path) current.moveNext();

			// check if the file is the same
			if (current &&
				current.getPath() == f.path && current.getHeader().startLine == 0 &&
				f.lastWriteTime == current.getHeader().timeStamp && f.fileSize == current.getHeader().fileSize)
			{
				// add this file and all subsequent chunks of the same file
				std::string contents = getCurrentFileContents(current);

				builder->appendFilePart(f.path.c_str(), 0, contents.c_str(), contents.size(), f.lastWriteTime, f.fileSize);
			}
			else
			{
				// grab file from fs
				builder->appendFile(f.path.c_str(), f.lastWriteTime, f.fileSize);
			}
		}
	}

	output->print("\n");
	
	if (!renameFile(tempPath.c_str(), targetPath.c_str()))
	{
		output->error("Error saving data file %s\n", targetPath.c_str());
		return;
	}
}