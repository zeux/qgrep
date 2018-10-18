#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <cassert>

template <typename T>
class BlockingQueue
{
public:
	BlockingQueue(): totalSize(0), totalSizeLimit(static_cast<size_t>(-1))
	{
	}

	explicit BlockingQueue(size_t limit): totalSize(0), totalSizeLimit(limit)
	{
	}

	void push(T value, size_t size = 0)
	{
		std::unique_lock<std::mutex> lock(mutex);

		itemsNotFull.wait(lock, [&]() { return !(totalSize != 0 && totalSize + size > totalSizeLimit); });

		Item item = {std::move(value), size};
		items.push(std::move(item));
		totalSize += size;

		lock.unlock();
		itemsNotEmpty.notify_one();
	}

	T pop()
	{
		std::unique_lock<std::mutex> lock(mutex);

		itemsNotEmpty.wait(lock, [&]() { return !items.empty(); });

		Item item = std::move(items.front());
		items.pop();

		if (item.size > 0)
		{
			assert(totalSize >= item.size);
			totalSize -= item.size;

			lock.unlock();
			itemsNotFull.notify_all();
		}

		return std::move(item.value);
	}

private:
	struct Item
	{
		T value;
		size_t size;
	};

	std::mutex mutex;
	std::condition_variable itemsNotEmpty;
	std::condition_variable itemsNotFull;

	std::queue<Item> items;
	size_t totalSize;
	size_t totalSizeLimit;
};
