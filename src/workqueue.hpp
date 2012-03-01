#ifndef WORKQUEUE_HPP
#define WORKQUEUE_HPP

#include <vector>
#include <thread>
#include <functional>

#include "blockingqueue.hpp"

struct WorkQueue
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

#endif