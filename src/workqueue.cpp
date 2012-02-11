#include "workqueue.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HANDLE gSemaphore;
static volatile long gItemCount;

static DWORD WINAPI queueWorker(void* context)
{
	ReleaseSemaphore(gSemaphore, 1, NULL);
	
	wqWorkItem* item = static_cast<wqWorkItem*>(context);
	
	item->run();
	delete item;
	
	InterlockedDecrement(&gItemCount);
	
	return 0;
}

void wqBegin(unsigned int maxQueueSize)
{
	gSemaphore = CreateSemaphore(NULL, maxQueueSize, maxQueueSize, NULL);
	gItemCount = 0;
}

void wqEnd()
{
	while (gItemCount != 0) Sleep(1);
	
	CloseHandle(gSemaphore);
}

void wqQueue(wqWorkItem* item)
{
	WaitForSingleObject(gSemaphore, INFINITE);
	InterlockedIncrement(&gItemCount);
	QueueUserWorkItem(queueWorker, item, 0);
}