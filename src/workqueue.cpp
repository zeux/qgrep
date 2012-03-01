#include "workqueue.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static void workerThreadFun(BlockingQueue<std::function<void()>>& queue)
{
	while (std::function<void()> fun = queue.pop())
	{
		fun();
	}
}

unsigned int WorkQueue::getIdealWorkerCount()
{
	SYSTEM_INFO info;
	GetSystemInfo(&info);

	return info.dwNumberOfProcessors;
}

WorkQueue::WorkQueue(size_t workerCount, size_t memoryLimit): queue(memoryLimit)
{
	for (size_t i = 0; i < workerCount; ++i)
		workers.emplace_back(workerThreadFun, std::ref(queue));
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