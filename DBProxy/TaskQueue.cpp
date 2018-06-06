#include "TaskQueue.h"

//=============================================//
//-	Task Queue
//=============================================//
TaskPackagePtr TaskQueue::pop() throw ()
{
	TaskPackagePtr ret = nullptr;
	try
	{
		ret = _queue->pop();
	}
	catch (const SafeQueue<TaskPackagePtr>::EmptyException &e)
	{
	}
	return ret;
}

//=============================================//
//-	Read/Write Task Queue
//=============================================//
RWTaskQueue::~RWTaskQueue()
{
	_wqueue.clear();
	_rqueue.clear();
}

TaskPackagePtr RWTaskQueue::pop() throw ()
{
	TaskPackagePtr ret;
	try
	{
		ret = _wqueue.pop();
	}
	catch (const SafeQueue<TaskPackagePtr>::EmptyException &e)
	{
		try
		{
			ret = _rqueue.pop();
		}
		catch (const SafeQueue<TaskPackagePtr>::EmptyException &e)
		{
		}
	}
	return ret;
}
