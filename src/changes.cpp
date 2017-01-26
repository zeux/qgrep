#include "common.hpp"
#include "changes.hpp"

#include "fileutil.hpp"
#include "filestream.hpp"

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
