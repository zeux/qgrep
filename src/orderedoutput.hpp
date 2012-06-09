#pragma once

#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>

#include "blockingqueue.hpp"

class Output;

class OrderedOutput
{
public:
	struct Chunk
	{
		unsigned int id;
		unsigned int lines;
		std::string result;

		Chunk(unsigned int id, unsigned int lines);
	};

	OrderedOutput(Output* output, size_t memoryLimit, size_t flushThreshold, unsigned int lineLimit);
	~OrderedOutput();

    Chunk* begin(unsigned int id);
    void write(Chunk* chunk, const char* format, ...);
    void end(Chunk* chunk);

	unsigned int getLineCount() const;

private:
	Output* output;

	size_t flushThreshold;
	unsigned int lineLimit;

	BlockingQueue<Chunk*> writeQueue;
	std::thread writeThread;

	std::mutex mutex;
	std::atomic<unsigned int> currentChunk;
	std::atomic<unsigned int> currentLine;
	std::map<unsigned int, Chunk*> chunks;
};
