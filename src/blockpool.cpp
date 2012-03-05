#include "blockpool.hpp"

#include <cassert>

static char* pop(std::vector<char*>& v)
{
	char* r = v.back();
	v.pop_back();
	return r;
}

BlockPool::BlockPool(size_t blockSize): blockSize(blockSize), liveBlocks(0)
{
}

BlockPool::~BlockPool()
{
	assert(liveBlocks == 0);

	for (size_t i = 0; i < blocks.size(); ++i)
		delete[] blocks[i];
}

std::shared_ptr<char> BlockPool::allocate(size_t size)
{
	if (size > blockSize)
		return std::shared_ptr<char>(new char[size]);

	std::lock_guard<std::mutex> lock(mutex);

	char* block = blocks.empty() ? new char[blockSize] : pop(blocks);

	liveBlocks++;

	return std::shared_ptr<char>(block, [this](char* block) {
		std::lock_guard<std::mutex> lock(this->mutex);

		assert(liveBlocks > 0);
		liveBlocks--;

		blocks.push_back(block);
	});
}