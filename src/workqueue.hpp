#ifndef WORKQUEUE_HPP
#define WORKQUEUE_HPP

void wqBegin(unsigned int maxQueueSize);
void wqEnd();

struct wqWorkItem
{
	virtual ~wqWorkItem() {}
	virtual void run() = 0;
};

void wqQueue(wqWorkItem* item);

template <typename T>
struct wqWorkItemT: wqWorkItem
{
	T pred;
	
	wqWorkItemT(const T& pred): pred(pred)
	{
	}
	
	virtual void run()
	{
		pred();
	}
};

template <typename T> void wqQueue(const T& pred)
{
    wqQueue(static_cast<wqWorkItem*>(new wqWorkItemT<T>(pred)));
}

#endif
