#include "common.hpp"
#include "watch.hpp"

#include "project.hpp"
#include "fileutil.hpp"
#include "filestream.hpp"
#include "output.hpp"

#include <set>
#include <thread>
#include <mutex>
#include <condition_variable>

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
		context->watchingThreads.emplace_back([=]
		{
			context->output->print("Watching folder %s...\n", path.c_str());

			if (!watchDirectory(path.c_str(), [=](const char* file) { fileChanged(context, group, path.c_str(), file); }))
				context->output->error("Error watching folder %s\n", path.c_str());

			context->output->print("No longer watching folder %s\n", path.c_str());
		});
	}

	for (auto& child: group->groups)
		startWatchingRec(context, child.get());
}

void watchProject(Output* output, const char* path)
{
	WatchContext context = { output };

	std::unique_ptr<ProjectGroup> group = parseProject(output, path);

	startWatchingRec(&context, group.get());

	std::thread([&] { updateThreadFunc(&context, path); }).swap(context.updateThread);

	for (auto& t: context.watchingThreads)
		t.join();

	context.updateThread.join();
}
