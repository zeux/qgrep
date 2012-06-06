#include "workqueue.hpp"

#include <functional>
#include <thread>

unsigned int WorkQueue::getIdealWorkerCount()
{
	return std::max(std::thread::hardware_concurrency(), 1u);
}

static void workerThreadFun(BlockingQueue<std::function<void()>>& queue)
{
	while (std::function<void()> fun = queue.pop())
	{
		fun();
	}
}

WorkQueue::WorkQueue(size_t workerCount, size_t memoryLimit): queue(memoryLimit)
{
	for (size_t i = 0; i < workerCount; ++i)
		workers.emplace_back(std::bind(workerThreadFun, std::ref(queue)));
}

WorkQueue::~WorkQueue()
{
	for (size_t i = 0; i < workers.size(); ++i)
		queue.push(std::function<void()>());

	for (size_t i = 0; i < workers.size(); ++i)
		workers[i].join();
}

void WorkQueue::push(std::function<void()> fun, size_t size)
{
	queue.push(fun, size);
}