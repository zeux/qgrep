#include "orderedoutput.hpp"

#include <assert.h>
#include <stdarg.h>

static void strprintf(std::string& result, const char* format, va_list args)
{
	int count = _vsnprintf_c(0, 0, format, args);
	assert(count >= 0);

	if (count > 0)
	{
		size_t offset = result.size();
		result.resize(offset + count);
		_vsnprintf(&result[offset], count, format, args);
	}
}

OrderedOutput::OrderedOutput(): current(0)
{
}

OrderedOutput::~OrderedOutput()
{
	assert(chunks.empty());
}

OrderedOutput::Chunk* OrderedOutput::begin(unsigned int id)
{
	MutexLock lock(mutex);

	assert(id >= current);

	Chunk& chunk = chunks[id];
	chunk.ready = false;

	return &chunk;
}

void OrderedOutput::write(Chunk* chunk, const char* format, ...)
{
	assert(!chunk->ready);

	va_list args;
	va_start(args, format);
	strprintf(chunk->result, format, args);
	va_end(args);

	// $$$ if the chunk is too large and it's the current one, we can flush it immediately,
	// as long as we do it via writer thread
}

void OrderedOutput::end(Chunk* chunk)
{
	chunk->ready = true;

	MutexLock lock(mutex);

	while (!chunks.empty() && chunks.begin()->first == current && chunks.begin()->second.ready)
	{
		Chunk& chunk = chunks.begin()->second;

		// $$$ this blocks the caller - we should send the chunks to the writer thread instead
		if (!chunk.result.empty())
			printf("%s", chunk.result.c_str());

		chunks.erase(chunks.begin());
		current++;
	}
}