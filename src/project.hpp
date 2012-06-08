#pragma once

#include <string>
#include <vector>

class Output;

std::string getProjectPath(const char* name);
std::vector<std::string> getProjects();
std::vector<std::string> getProjectPaths(const char* list);

bool getProjectFiles(Output* output, const char* path, std::vector<std::string>& files);