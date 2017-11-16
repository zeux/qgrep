#include "common.hpp"
#include "changes.hpp"

#include "fileutil.hpp"
#include "filestream.hpp"
#include "output.hpp"
#include "project.hpp"

#include <algorithm>
#include <fstream>

std::vector<std::string> readChanges(const char* path)
{
	std::string filePath = replaceExtension(path, ".qgc");

	std::vector<std::string> result;
	std::string line;

	std::ifstream in(filePath.c_str(), std::ios::in);

	while (std::getline(in, line))
		result.push_back(line);

	return result;
}

bool writeChanges(const char* path, const std::vector<std::string>& files)
{
	std::string targetPath = replaceExtension(path, ".qgc");
	std::string tempPath = targetPath + "_";

	{
		FileStream out(tempPath.c_str(), "wb");
		if (!out)
			return false;

		for (auto& f: files)
		{
			out.write(f.data(), f.size());
			out.write("\n", 1);
		}
	}

	return renameFile(tempPath.c_str(), targetPath.c_str());
}

static bool isFileInProjectGroupRec(ProjectGroup* group, const std::string& file)
{
	for (auto& path: group->paths)
		if (file.length() > path.length() && file.compare(0, path.length(), path) == 0)
			if (isFileAcceptable(group, file.c_str()))
				return true;

	for (auto& child: group->groups)
		if (isFileInProjectGroupRec(child.get(), file))
			return true;

	return false;
}

void appendChanges(Output* output, const char* path, const std::vector<std::string>& files)
{
	std::unique_ptr<ProjectGroup> group = parseProject(output, path);
	if (!group)
		return;

	bool writeNeeded = false;
	std::vector<std::string> changes = readChanges(path);

	for (auto& file: files)
	{
		std::string nf = normalizePath(getCurrentDirectory().c_str(), file.c_str());

		if (isFileInProjectGroupRec(group.get(), nf.c_str()))
		{
			writeNeeded = true;
			changes.push_back(nf);
		}
	}

	if (writeNeeded)
	{
		std::sort(changes.begin(), changes.end());
		changes.erase(std::unique(changes.begin(), changes.end()), changes.end());

		if (!writeChanges(path, changes))
			output->error("Error writing changes for project %s\n", path);
	}
}
