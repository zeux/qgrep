#include "orderedoutput.hpp"

#include "output.hpp"

#include <functional>
#include <cassert>
#include <cstdarg>

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

static void writeThreadFun(Output* output, BlockingQueue<OrderedOutput::Chunk*>& queue)
{
	while (OrderedOutput::Chunk* chunk = queue.pop())
	{
		output->print("%s", chunk->result.c_str());
		delete chunk;
	}
}

OrderedOutput::OrderedOutput(Output* output, size_t memoryLimit, size_t flushThreshold):
	output(output), writeQueue(memoryLimit), writeThread(std::bind(writeThreadFun, output, std::ref(writeQueue))), flushThreshold(flushThreshold), currentChunk(0)
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
	assert(id >= currentChunk);

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

	if (chunk->result.size() > flushThreshold && chunk->id == currentChunk)
	{
		Chunk* temp = new Chunk;
		temp->result.swap(chunk->result);
		writeQueue.push(temp, temp->result.size());
	}
}

void OrderedOutput::end(Chunk* chunk)
{
	std::lock_guard<std::mutex> lock(mutex);

	assert(chunks[chunk->id] == 0);
	chunks[chunk->id] = chunk;

	while (!chunks.empty() && chunks.begin()->first == currentChunk)
	{
		Chunk* chunk = chunks.begin()->second;
		chunks.erase(chunks.begin());
		currentChunk++;

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
