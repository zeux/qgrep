#include "common.hpp"
#include "orderedoutput.hpp"

#include "output.hpp"
#include "stringutil.hpp"

#include <functional>

static void writeThreadFun(Output* output, BlockingQueue<OrderedOutput::Chunk*>& queue, unsigned int limit)
{
	unsigned int total = 0;

	while (OrderedOutput::Chunk* chunk = queue.pop())
	{
		std::string::size_type pos = 0;

		for (unsigned int i = total; i < limit && pos < chunk->result.length(); ++i)
		{
			const char* line = chunk->result.c_str() + pos;
			size_t length = strlen(line);

			output->rawprint(line, length);
			total++;
			pos += length + 1;
		}

		delete chunk;
	}
}

OrderedOutput::Chunk::Chunk(unsigned int id, unsigned int lines): id(id), lines(lines)
{
}

OrderedOutput::OrderedOutput(Output* output, size_t memoryLimit, size_t flushThreshold, unsigned int lineLimit):
	output(output), flushThreshold(flushThreshold), lineLimit(lineLimit), writeQueue(memoryLimit),
	writeThread(std::bind(writeThreadFun, output, std::ref(writeQueue), lineLimit)),
	currentChunk(0), currentLine(0)
{
}

OrderedOutput::~OrderedOutput()
{
	writeQueue.push(nullptr);
	writeThread.join();

	assert(chunks.empty());

    (void)output;
}

OrderedOutput::Chunk* OrderedOutput::begin(unsigned int id)
{
	assert(id >= currentChunk);

	return new Chunk(id, 0);
}

void OrderedOutput::write(Chunk* chunk, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	strprintf(chunk->result, format, args);
	va_end(args);

	write(chunk);
}

void OrderedOutput::write(Chunk* chunk)
{
	chunk->result.push_back(0);
	chunk->lines++;

	if (chunk->result.size() > flushThreshold && chunk->id == currentChunk)
	{
		Chunk* temp = new Chunk(chunk->id, chunk->lines);
		chunk->result.swap(temp->result);
		chunk->lines = 0;

		currentLine += temp->lines;
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

		if (chunk->result.empty())
		{
			delete chunk;
		}
		else
		{
			currentLine += chunk->lines;
			writeQueue.push(chunk, chunk->result.size());
		}

		currentChunk++;
	}
}

unsigned int OrderedOutput::getLineCount() const
{
	return std::min(currentLine.load(), lineLimit);
}
