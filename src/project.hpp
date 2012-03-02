#pragma once

#include <string>
#include <vector>

std::string getProjectPath(const char* name);
std::vector<std::string> getProjects();

bool getProjectFiles(const char* path, std::vector<std::string>& files);