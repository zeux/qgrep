#ifndef ORDEREDOUTPUT_HPP
#define ORDEREDOUTPUT_HPP

#include <string>
#include <map>
#include <mutex>
#include <thread>

#include "blockingqueue.hpp"

class OrderedOutput
{
public:
	struct Chunk
	{
		unsigned int id;
		std::string result;
	};

	explicit OrderedOutput(size_t memoryLimit);
	~OrderedOutput();

    Chunk* begin(unsigned int id);
    void write(Chunk* chunk, const char* format, ...);
    void end(Chunk* chunk);

private:
	BlockingQueue<Chunk*> writeQueue;
	std::thread writeThread;

	std::mutex mutex;
	unsigned int current;
	std::map<unsigned int, Chunk*> chunks;
};

#endif
