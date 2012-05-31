extern "C" __declspec(dllimport) void entryPointConsole(int argc, const char** argv);

int main(int argc, const char** argv)
{
	entryPointConsole(argc, argv);
}