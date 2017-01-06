#pragma once

#include <string>
#include <vector>
#include <memory>

class Output;
class Regex;

std::string getProjectPath(const char* name);
std::vector<std::string> getProjects();
std::vector<std::string> getProjectPaths(const char* list);

struct ProjectGroup
{
	ProjectGroup* parent;

	std::vector<std::string> paths;
	std::vector<std::string> files;
	std::shared_ptr<Regex> include;
	std::shared_ptr<Regex> exclude;

	std::vector<std::unique_ptr<ProjectGroup>> groups;
};

std::unique_ptr<ProjectGroup> parseProject(Output* output, const char* file);
bool isFileAcceptable(ProjectGroup* group, const char* path);

struct FileInfo
{
	std::string path;
	uint64_t lastWriteTime;
	uint64_t fileSize;

	FileInfo() {}
	FileInfo(const std::string& path, uint64_t lastWriteTime, uint64_t fileSize): path(path), lastWriteTime(lastWriteTime), fileSize(fileSize) {}
};

bool getProjectFiles(Output* output, const char* path, std::vector<FileInfo>& files);
