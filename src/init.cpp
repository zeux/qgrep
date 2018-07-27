#include "common.hpp"
#include "init.hpp"

#include "output.hpp"
#include "fileutil.hpp"

#include <fstream>

static const char* kDefaultLanguages[] =
{
	"C/C++", "cpp|cxx|cc|c|hpp|hxx|hh|h|inl",
	"D", "d",
	"F#, OCaml, Haskell", "fs|fsi|fsx|ml|mli|hs",
	"HTML", "htm|html",
	"Java, C#, VB.NET", "java|cs|vb",
	"Lua, Squirrel", "lua|nut",
	"Nim", "nim",
	"Objective C/C++", "m|mm",
	"Perl, Python, Ruby", "pl|py|pm|rb",
	"PHP, JavaScript, ActionScript", "php|js|as",
	"Shaders", "hlsl|glsl|cg|fx|cgfx",
};

static bool fileExists(const char* path)
{
	std::ifstream in(path);

	return !!in;
}

void initProject(Output* output, const char* name, const char* file, const char* path)
{
	if (fileExists(file))
	{
		output->error("Error: project %s already exists\n", file);
		return;
	}

	createPathForFile(file);

	std::ofstream out(file);
	if (!out)
	{
		output->error("Error opening project file %s for writing\n", file);
		return;
	}

	std::string cwd = getCurrentDirectory();
	std::string npath = normalizePath(cwd.c_str(), path);
	
	out << "path " << npath << std::endl << std::endl;

	for (size_t i = 0; i + 1 < sizeof(kDefaultLanguages) / sizeof(kDefaultLanguages[0]); i += 2)
	{
		out << "# " << kDefaultLanguages[i] << std::endl;
		out << "include \\.(" << kDefaultLanguages[i + 1] << ")$" << std::endl;
		out << "# exclude something/$" << std::endl;
		out << std::endl;
	}

	output->print("Project file %s created, run `qgrep update %s` to build\n", file, name);
}
