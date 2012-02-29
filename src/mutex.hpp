#ifndef MUTEX_HPP
#define MUTEX_HPP

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class Mutex
{
public:
	Mutex()
	{
		InitializeCriticalSection(&cs);
	}

	~Mutex()
	{
		DeleteCriticalSection(&cs);
	}

	void lock()
	{
		EnterCriticalSection(&cs);
	}

	void unlock()
	{
		LeaveCriticalSection(&cs);
	}

private:
	CRITICAL_SECTION cs;
};

class MutexLock
{
public:
	MutexLock(Mutex& mutex): mutex(mutex)
	{
		mutex.lock();
	}

	~MutexLock()
	{
		mutex.unlock();
	}

private:
	Mutex& mutex;
};

#endif
