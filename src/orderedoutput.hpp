#ifndef ORDEREDOUTPUT_HPP
#define ORDEREDOUTPUT_HPP

#include <string>
#include <map>

#include "mutex.hpp"

class OrderedOutput
{
public:
	struct Chunk
	{
		bool ready;
		std::string result;
	};

	OrderedOutput();
	~OrderedOutput();

    Chunk* begin(unsigned int id);
    void write(Chunk* chunk, const char* format, ...);
    void end(Chunk* chunk);

private:
	Mutex mutex;
	unsigned int current;
	std::map<unsigned int, Chunk> chunks;
};

#endif
