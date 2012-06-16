#pragma once

#include <string>
#include <vector>
#include <stdint.h>

class Output;

std::string getProjectPath(const char* name);
std::vector<std::string> getProjects();
std::vector<std::string> getProjectPaths(const char* list);

struct FileInfo
{
	std::string path;
	uint64_t lastWriteTime;
	uint64_t fileSize;

	FileInfo() {}
	FileInfo(const std::string& path, uint64_t lastWriteTime, uint64_t fileSize): path(path), lastWriteTime(lastWriteTime), fileSize(fileSize) {}
};

bool getProjectFiles(Output* output, const char* path, std::vector<FileInfo>& files);