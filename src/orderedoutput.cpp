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

static void writeThreadFun(BlockingQueue<OrderedOutput::Chunk*>& queue)
{
	while (OrderedOutput::Chunk* chunk = queue.pop())
	{
		printf("%s", chunk->result.c_str());
		delete chunk;
	}
}

OrderedOutput::OrderedOutput(size_t memoryLimit): writeQueue(memoryLimit), writeThread(writeThreadFun, std::ref(writeQueue)), current(0)
{
}

OrderedOutput::~OrderedOutput()
{
	writeQueue.push(0);
	writeThread.join();

	assert(chunks.empty());
}

OrderedOutput::Chunk* OrderedOutput::begin(unsigned int id)
{
	assert(id >= current);

	Chunk* chunk = new Chunk;
	chunk->id = id;

	return chunk;
}

void OrderedOutput::write(Chunk* chunk, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	strprintf(chunk->result, format, args);
	va_end(args);

	// $$$ if the chunk is too large and it's the current one, we can flush it immediately,
	// as long as we do it via writer thread
}

void OrderedOutput::end(Chunk* chunk)
{
	std::lock_guard<std::mutex> lock(mutex);

	assert(chunks[chunk->id] == 0);
	chunks[chunk->id] = chunk;

	while (!chunks.empty() && chunks.begin()->first == current)
	{
		Chunk* chunk = chunks.begin()->second;
		chunks.erase(chunks.begin());
		current++;

		if (chunk->result.empty())
		{
			delete chunk;
		}
		else
		{
			writeQueue.push(chunk, chunk->result.size());
		}
	}
}