#ifndef Task_Queue_h
#define Task_Queue_h

#include "SafeQueue.hpp"
#include "TaskPackage.h"
#include "IMySQLTaskQueue.h"

using namespace fpnn;

//---------------------------------------------//
//-	Task Queue
//---------------------------------------------//
class TaskQueue: public IMySQLTaskQueue
{
	SafeQueue<TaskPackagePtr> *_queue;
	
public:
	TaskQueue(SafeQueue<TaskPackagePtr> *queue): _queue(queue) {}
	virtual ~TaskQueue() {}
	
	virtual bool empty() { return _queue->empty(); }
	virtual size_t size() { return _queue->size(); }
	virtual void clear() {}	//-- do nothing. Because many thread pool will sharing a queue.

	virtual TaskPackagePtr pop() throw ();
};

//---------------------------------------------//
//-	Read/Write Task Package
//---------------------------------------------//
class RWTaskQueue: public IMySQLTaskQueue
{
	SafeQueue<TaskPackagePtr> _rqueue;
	SafeQueue<TaskPackagePtr> _wqueue;
	
	TaskQueue _rqueueWrapper;
	
public:
	RWTaskQueue(): _rqueue(), _wqueue(), _rqueueWrapper(&_rqueue) {}
	virtual ~RWTaskQueue();
	
	virtual bool empty() { return _rqueue.empty() ? _wqueue.empty() : false; }
	virtual size_t size() { return _rqueue.size() + _wqueue.size(); }
	virtual size_t readQueueSize() { return _rqueue.size(); }
	virtual size_t writeQueueSize() { return _wqueue.size(); }
	virtual void clear() {} //-- do nothing. Because many thread pool will sharing this queue.

	virtual TaskPackagePtr pop() throw ();
	inline void push(TaskPackagePtr task, bool readTask)
	{
		readTask ? _rqueue.push(task) : _wqueue.push(task);
	}
	
	TaskQueue* readQueue() { return &_rqueueWrapper; }
};

#endif
