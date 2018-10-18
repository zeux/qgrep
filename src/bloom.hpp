#pragma once

inline unsigned int ngram(char a, char b, char c, char d)
{
    return (static_cast<unsigned char>(a) << 24) + (static_cast<unsigned char>(b) << 16) + (static_cast<unsigned char>(c) << 8) + static_cast<unsigned char>(d);
}

// 6-shift variant from http://burtleburtle.net/bob/hash/integer.html
inline unsigned int bloomHash1(unsigned int v)
{
    v = (v+0x7ed55d16) + (v<<12);
    v = (v^0xc761c23c) ^ (v>>19);
    v = (v+0x165667b1) + (v<<5);
    v = (v+0xd3a2646c) ^ (v<<9);
    v = (v+0xfd7046c5) + (v<<3);
    v = (v^0xb55a4f09) ^ (v>>16);
    return v;
}

// variant A from https://github.com/strixinteractive/inthash
inline unsigned int bloomHash2(unsigned int v)
{
    v *= 1193897147;
    v ^= v >> 16;
    v ^= v >> 14;
    v += 1193897147;
    return v;
}

inline void bloomFilterUpdate(unsigned char* data, unsigned int size, unsigned int value, unsigned int iterations)
{
    unsigned int h1 = bloomHash1(value);
    unsigned int h2 = bloomHash2(value);
    unsigned int hv = h1;

    for (unsigned int i = 0; i < iterations; ++i)
    {
        hv += h2;
        unsigned int h = hv % (size * 8);

        data[h / 8] |= 1 << (h % 8);
    }
}

inline bool bloomFilterExists(const unsigned char* data, unsigned int size, unsigned int value, unsigned int iterations)
{
    unsigned int h1 = bloomHash1(value);
    unsigned int h2 = bloomHash2(value);
    unsigned int hv = h1;

    for (unsigned int i = 0; i < iterations; ++i)
    {
        hv += h2;
        unsigned int h = hv % (size * 8);

        if (data[h / 8] & (1 << (h % 8)))
            ;
        else
            return false;
    }

    return true;
}
