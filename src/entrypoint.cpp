extern "C"
#ifdef _WIN32
__declspec(dllimport)
#endif
void qgrepConsole(int argc, const char** argv);

int main(int argc, const char** argv)
{
	qgrepConsole(argc, argv);
}
