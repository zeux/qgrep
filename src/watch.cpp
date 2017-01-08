#include "common.hpp"
#include "watch.hpp"

#include "project.hpp"
#include "fileutil.hpp"
#include "filestream.hpp"
#include "output.hpp"
#include "format.hpp"
#include "compression.hpp"

#include <set>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <string.h>

struct WatchContext
{
	Output* output;

	std::thread updateThread;
	std::vector<std::thread> watchingThreads;

	std::set<std::string> changedFiles;
	std::mutex changedFilesMutex;
	std::condition_variable changedFilesChanged;
};

static void fileChanged(WatchContext* context, ProjectGroup* group, const char* path, const char* file)
{
	if (isFileAcceptable(group, file))
	{
		std::string npath = normalizePath(path, file);

		context->output->print("Change %s\n", npath.c_str());

		std::unique_lock<std::mutex> lock(context->changedFilesMutex);

		context->changedFiles.insert(npath);
		context->changedFilesChanged.notify_one();
	}
	else
	{
		std::string npath = normalizePath(path, file);

		context->output->print("Ignore %s\n", npath.c_str());
	}
}

static void updateThreadFunc(WatchContext* context, const char* path)
{
	std::string targetPath = replaceExtension(path, ".qgc");
	std::string tempPath = targetPath + "_";

	std::vector<std::string> changedFiles;

	for (;;)
	{
		{
			std::unique_lock<std::mutex> lock(context->changedFilesMutex);

			context->changedFilesChanged.wait(lock, [&] { return context->changedFiles.size() != changedFiles.size(); });

			changedFiles.assign(context->changedFiles.begin(), context->changedFiles.end());
		}

		context->output->print("Flush %d\n", int(changedFiles.size()));

		{
			FileStream out(tempPath.c_str(), "wb");
			if (!out)
			{
				context->output->error("Error saving changes to %s\n", tempPath.c_str());
				continue;
			}

			for (auto& f : changedFiles)
			{
				out.write(f.data(), f.size());
				out.write("\n", 1);
			}
		}

		if (!renameFile(tempPath.c_str(), targetPath.c_str()))
		{
			context->output->error("Error saving changes to %s\n", targetPath.c_str());
			continue;
		}
	}
}

static void startWatchingRec(WatchContext* context, ProjectGroup* group)
{
	for (auto& path : group->paths)
	{
		context->output->print("Watching folder %s...\n", path.c_str());

		context->watchingThreads.emplace_back([=]
		{
			if (!watchDirectory(path.c_str(), [=](const char* file) { fileChanged(context, group, path.c_str(), file); }))
				context->output->error("Error watching folder %s\n", path.c_str());

			context->output->print("No longer watching folder %s\n", path.c_str());
		});
	}

	for (auto& child: group->groups)
		startWatchingRec(context, child.get());
}

static bool getDataFileList(Output* output, const char* path, std::vector<FileInfo>& result)
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
		output->error("Error reading data file %s: file format is out of date, update the project to fix\n", path);
		return false;
	}

	DataChunkHeader chunk;
	std::vector<char> data;

	while (read(in, chunk))
	{
		try
		{
			data.resize(chunk.compressedSize + chunk.uncompressedSize);
		}
		catch (const std::bad_alloc&)
		{
			output->error("Error reading data file %s: malformed chunk\n", path);
			return false;
		}

		in.skip(chunk.extraSize);
		in.skip(chunk.indexSize);

		if (!read(in, data.data(), chunk.compressedSize))
		{
			output->error("Error reading data file %s: malformed chunk\n", path);
			return false;
		}

		char* uncompressed = data.data() + chunk.compressedSize;

		decompressPartial(uncompressed, chunk.uncompressedSize, data.data(), chunk.compressedSize, chunk.fileTableSize);

		const DataChunkFileHeader* files = reinterpret_cast<const DataChunkFileHeader*>(uncompressed);

		for (unsigned int i = 0; i < chunk.fileCount; ++i)
		{
			const DataChunkFileHeader& file = files[i];

			FileInfo fi = { std::string(uncompressed + file.nameOffset, file.nameLength), file.timeStamp, file.fileSize };

			if (result.empty() || result.back().path != fi.path)
				result.push_back(fi);
		}
	}

	return true;
}

void watchProject(Output* output, const char* path)
{
	WatchContext context = { output };

    output->print("Watching %s:\n", path);

	std::unique_ptr<ProjectGroup> group = parseProject(output, path);
	if (!group)
		return;

	startWatchingRec(&context, group.get());

	output->print("Scanning project...\r");

	std::vector<FileInfo> files = getProjectGroupFiles(output, group.get());

	output->print("Reading data pack...\r");

	std::vector<FileInfo> packFiles;
	if (!getDataFileList(output, replaceExtension(path, ".qgd").c_str(), packFiles))
		return;

	std::vector<std::string> changedFiles;
	unsigned int filesAdded = 0;
	unsigned int filesRemoved = 0;
	unsigned int filesChanged = 0;

	for (size_t i = 0, j = 0; i < files.size() || j < packFiles.size(); )
	{
		if (i < files.size() && j < packFiles.size())
		{
			if (files[i].path == packFiles[j].path)
			{
				if (files[i].fileSize != packFiles[j].fileSize || files[i].timeStamp != packFiles[j].timeStamp)
				{
					changedFiles.push_back(files[i].path);
					filesChanged++;
				}

				i++;
				j++;
			}
			else
			{
				if (files[i].path < packFiles[j].path)
				{
					changedFiles.push_back(files[i].path);
					filesAdded++;
					i++;
				}
				else
				{
					// TODO: handle removed files
					filesRemoved++;
					j++;
				}
			}
		}
		else if (i < files.size())
		{
			changedFiles.push_back(files[i].path);
			filesAdded++;
			i++;
		}
		else if (j < packFiles.size())
		{
			// TODO: handle removed files
			filesRemoved++;
			j++;
		}
	}

	removeFile(replaceExtension(path, ".qgc").c_str());

	{
		std::unique_lock<std::mutex> lock(context.changedFilesMutex);

		context.changedFiles.insert(changedFiles.begin(), changedFiles.end());
	}

	if (filesAdded) output->print("+%d ", filesAdded);
	if (filesRemoved) output->print("-%d ", filesRemoved);
	if (filesChanged) output->print("*%d ", filesChanged);
	output->print("%s; listening for further changes\n",
		(filesAdded || filesRemoved || filesChanged) ? "files" : "No changes");

	std::thread([&] { updateThreadFunc(&context, path); }).swap(context.updateThread);

	for (auto& t: context.watchingThreads)
		t.join();

	context.updateThread.join();
}
