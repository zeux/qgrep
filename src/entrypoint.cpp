extern "C"
#ifdef _WIN32
__declspec(dllimport)
#endif
void entryPointConsole(int argc, const char** argv);

#include <stdlib.h>

int main(int argc, const char** argv)
{
	_putenv_s("HOME", "C:/Users/Zeux");

	// for (int i = 0; i < 1000; ++i)
	entryPointConsole(argc, argv);
}
