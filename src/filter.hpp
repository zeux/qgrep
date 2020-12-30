// This file is part of qgrep and is distributed under the MIT license, see LICENSE.md
#pragma once

class Output;

struct FilterEntry
{
    size_t offset;
    size_t length;
};

struct FilterEntries
{
    const char* buffer;
    size_t bufferSize;

    FilterEntry* entries;
    unsigned int entryCount;
};

unsigned int filter(Output* output, const char* string, unsigned int options, unsigned int limit, const FilterEntries& entries, const FilterEntries* names = 0);

