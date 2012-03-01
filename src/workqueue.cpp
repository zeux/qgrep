#include "workqueue.hpp"

#include <functional>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

unsigned int WorkQueue::getIdealWorkerCount()
{
	SYSTEM_INFO info;
	GetSystemInfo(&info);

	return info.dwNumberOfProcessors;
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
		queue.push(0);

	for (size_t i = 0; i < workers.size(); ++i)
		workers[i].join();
}

void WorkQueue::push(std::function<void()> fun, size_t size)
{
	queue.push(fun, size);
}