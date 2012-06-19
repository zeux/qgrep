extern "C"
#ifdef _WIN32
__declspec(dllimport)
#endif
void entryPointConsole(int argc, const char** argv);

int main(int argc, const char** argv)
{
	entryPointConsole(argc, argv);
}
