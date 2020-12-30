// This file is part of qgrep and is distributed under the MIT license, see LICENSE.md
#pragma once

#include <vector>
#include <thread>
#include <functional>

#include "blockingqueue.hpp"

class WorkQueue
{
public:
	static unsigned int getIdealWorkerCount();

	WorkQueue(size_t workerCount, size_t memoryLimit);
	~WorkQueue();

	void push(std::function<void()> fun, size_t size = 0);

private:
	BlockingQueue<std::function<void()>> queue;
	std::vector<std::thread> workers;
};
