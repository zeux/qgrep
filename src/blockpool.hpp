#pragma once

#include <mutex>
#include <vector>
#include <memory>

class BlockPool
{
public:
    BlockPool(size_t blockSize);
	~BlockPool();

	std::shared_ptr<char> allocate(size_t size);

private:
    size_t blockSize;

	std::mutex mutex;
	std::vector<char*> blocks;
	size_t liveBlocks;
};