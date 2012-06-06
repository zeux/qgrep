#pragma once

inline unsigned int trigram(char a, char b, char c)
{
	return static_cast<unsigned char>(a) * 256 * 256 + static_cast<unsigned char>(b) * 256 + static_cast<unsigned char>(c);
}

inline unsigned int hashIteration(unsigned int v)
{
    v *= 1193897147;
    v ^= v >> 20;
    v += 1193897147;
    v ^= v >> 9;
	return v;
}

const unsigned int kBloomHashIterations = 8;

inline void bloomFilterUpdate(unsigned char* data, unsigned int size, unsigned int value)
{
	for (unsigned int i = 0; i < kBloomHashIterations; ++i)
	{
		value = hashIteration(value);
		unsigned int h = value % (size * 8);
		data[h / 8] |= 1 << (h % 8);
	}
}

inline bool bloomFilterExists(const unsigned char* data, unsigned int size, unsigned int value)
{
	for (unsigned int i = 0; i < kBloomHashIterations; ++i)
	{
		value = hashIteration(value);
		unsigned int h = value % (size * 8);

		if (data[h / 8] & (1 << (h % 8)))
			;
		else
			return false;
	}

	return true;
}