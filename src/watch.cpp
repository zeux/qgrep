#include "common.hpp"
#include "watch.hpp"

#include "project.hpp"
#include "fileutil.hpp"
#include "output.hpp"

#include <thread>

struct WatchContext
{
	Output* output;

	std::vector<std::thread> watchingThreads;
};

static void fileChanged(WatchContext* context, ProjectGroup* group, const char* path, const char* file)
{
	if (isFileAcceptable(group, file))
	{
		context->output->print("File changed: %s\n", file);
	}
	else
	{
		context->output->print("File changed: %s (ignore)\n", file);
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

	for (auto& t: context.watchingThreads)
		t.join();
}
