#ifdef _WIN32
extern "C" __declspec(dllimport)
#endif
void entryPointConsole(int argc, const char** argv);

int main(int argc, const char** argv)
{
	entryPointConsole(argc, argv);
}
