#include "common.hpp"
#include "watch.hpp"

#include "project.hpp"
#include "fileutil.hpp"

void watchProject(Output* output, const char* path)
{
	std::unique_ptr<ProjectGroup> group = parseProject(output, path);
}
