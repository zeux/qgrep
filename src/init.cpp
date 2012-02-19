#include "common.hpp"

#include <fstream>

void initProject(const char* file, const char* path)
{
	std::ofstream out(file);
	if (!out) fatal("Error opening project file %s for writing\n", file);
	
	out << "path " << path << std::endl;
	out << "include \\.(cpp|cxx|cc|c|hpp|hxx|hh|h|inl|py|pl|pm|js|as|hlsl|cg|fx)$" << std::endl;
}