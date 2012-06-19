#include "common.hpp"
#include "stringutil.hpp"

#ifdef _MSC_VER
	#define vsnprintf _vsnprintf_c
#endif

void strprintf(std::string& result, const char* format, va_list args)
{
	int count = vsnprintf(0, 0, format, args);
	assert(count >= 0);

	if (count > 0)
	{
		size_t offset = result.size();
		result.resize(offset + count + 1);

		vsnprintf(&result[offset], count + 1, format, args);

		assert(result[offset + count] == 0);
		result.resize(offset + count);
	}
}
