#include <sstream>
#include "msec.h"
#include "AutoRelease.h"
#include "MySQLClient.h"
#include "TaskPackage.h"
#include "TableManager.h"
#include "MySQLTaskThreadPool.h"

/*===============================================================================
FUNCTION DEFINITIONS: Thread Pool Functions: Class CThreadPool
=============================================================================== */
/*===========================================================================

FUNCTION: ThreadPool::Init

DESCRIPTION:
	Initialize the thread pool.

PARAMETERS:
	clsProcessor [in] - The instance of IProcessor which deal with the task and is called by the thread in the pool.
	ulInitNum    [in] - The number of threads appended into the pool after the pool created.
	ulMaxNum     [in] - The maximum number of threads held by the pool. If don't use the restriction, please let the value is ZERO.
	ulIncNum     [in] - The number of threads appended in the appending operation.
	ulPerfectNum [in] - The soft restriction. If the number of threads beyond this restriction, the pool will stop the excess threads
	                    and return the relative resource to the operating ststem rather than continue holding them after their tasks
						are finished.
	bDetached    [in] - Create the detached thread.
	bMemoryPoolUsed [in] - Using inside memory pool to manage memories.

RETURN VALUE:
	E_THREAD_POOL__MUTEX_INIT - Thread pool initialize failed. The mutex initialize failed.
	E_THREAD_POOL__MEMORY_POOL_INIT - The inside memory pool initialize failed.
	E_THREAD_POOL__ALL_THREAD_CREATE - Thread pool initialize failed. No threads can't be created.
	E_SUCCESS - Succeed.
===========================================================================*/
void MySQLTaskThreadPool::ReviseDataRelation()
{
	if (_maxCount > 0)
	{
		if(_maxCount < _initCount)
			_maxCount = _initCount;

		if (_perfectCount > _maxCount)
			_perfectCount = _maxCount;
	}

	
	if (_perfectCount < _initCount)
		_perfectCount = _initCount;
}
//=================================
bool MySQLTaskThreadPool::init(int32_t initCount, int32_t perAppendCount, int32_t perfectCount, int32_t maxCount, size_t tempThreadLatencySeconds)
{
	std::unique_lock<std::mutex> lck(_mutex);
	
	if (_inited)
		return true;

	_initCount = initCount;
	_maxCount = maxCount;
	_appendCount = perAppendCount;
	_perfectCount = perfectCount;

	_tempThreadLatencySeconds = tempThreadLatencySeconds;

	_tempThreadCount = 0;
	_busyThreadCount = 0;
	_normalThreadCount = 0;

	_willExit = false;

	ReviseDataRelation();

	for (int32_t i = 0; i < _initCount; i++)
	{
		_threadList.push_back(std::thread(&MySQLTaskThreadPool::process, this));
		_normalThreadCount += 1;
	}

	_inited = true;

	return true;
}

/*===========================================================================

FUNCTION: ThreadPool::WakeUp

DESCRIPTION:
  Wake up a thread to run task. (Call the thread in the pool.)

PARAMETERS:
	void * lpvoid [in] - The paramer is delivered to the executive function (IProcessor::Processor()).

RETURN VALUE:
   --[ Task not be executed status ]--
   E_THREAD_POOL__UNINITED - The thread pool need to be initialize, or the pool is exiting has exited.
   E_THREAD_POOL__WAKEUP_GAIN_NODE - Get the task package memory for wake up threads failed.
   --[ Task will be executed status ]--
   E_THREAD_POOL__WAKEUP_APPEND_THREAD_MAX_LIMIT - Append thread failed, the hard limitation is touched.
   E_THREAD_POOL__WAKEUP_APPEND_GAIN_NODE - Append thread failed, get the memory for append threads failed.
   E_THREAD_POOL__WAKEUP_APPEND_THREAD - Append thread failed, thread created failed.
   E_SUCCESS - Succeed.

NOTICE:
   If returned values are changed, please modify another WakeUp() function in head files for identical behavior.
===========================================================================*/
bool MySQLTaskThreadPool::wakeUp()
{
	if (!_inited)
		return false;

	std::unique_lock<std::mutex> lck(_mutex);

	if (_willExit)
		return false;

	int queueSize = (int)_taskQueue->size();
	if (_busyThreadCount + queueSize > (_normalThreadCount + _tempThreadCount))
		append();		

	_condition.notify_one();

	return true;
}

/*===========================================================================

FUNCTION: ThreadPool::Append

DESCRIPTION:
  Append new threads into the pool.

PARAMETERS:
	None.

RETURN VALUE:
   E_THREAD_POOL__WAKEUP_APPEND_THREAD_MAX_LIMIT - Append failed, the hard limitation is touched.
   E_THREAD_POOL__WAKEUP_APPEND_GAIN_NODE - Append failed, get the memory for append threads failed.
   E_THREAD_POOL__WAKEUP_APPEND_THREAD - Append failed, thread created failed.
   E_SUCCESS - Succeed.
===========================================================================*/
bool MySQLTaskThreadPool::append()
{
	if (_appendCount == 0)
		return false;

	if (_normalThreadCount >= _perfectCount)
	{
		if (_maxCount > 0)
		{
			if (_tempThreadCount + _normalThreadCount >= _maxCount)
				return false;
		}

		std::thread(&MySQLTaskThreadPool::temporaryProcess, this).detach();
		_tempThreadCount += 1;

		return true;
	}
	else
	{
		int32_t diff = _perfectCount - _normalThreadCount;
		int32_t appendCount = (diff >= _appendCount) ? _appendCount : diff;

		for (int32_t i = 0; i < appendCount; i++)
		{
			_threadList.push_back(std::thread(&MySQLTaskThreadPool::process, this));
			_normalThreadCount += 1;
		}
	}

	return true;
}

/*===========================================================================

FUNCTION: ThreadPool::Process

DESCRIPTION:
 The thread function of thread pool. (Include both normal threads and temporary threads.)

PARAMETERS:
	lpParam [in] - Pointer a ThreadPool::TEMPPARAM structure which takes information of thread attribute.

RETURN VALUE:
   NULL - Thread exits.
===========================================================================*/
void MySQLTaskThreadPool::process()
{
	MySQLClient *mySQL = new MySQLClient(_dbInfo->host, _dbInfo->port, _dbInfo->username,
		_dbInfo->password, _dbInfo->databaseName, _dbInfo->timeout);

	AutoDeleteGuard<MySQLClient> adg(mySQL);

	while (true)
	{
		std::shared_ptr<TaskPackage> task;
		while (true)
		{
			task = _taskQueue->pop();
			if (task)
				break;

			std::unique_lock<std::mutex> lck(_mutex);
			if (_willExit)
			{
				_normalThreadCount -= 1;
				return;
			}
			_condition.wait(lck);
		}
		
		{
			std::unique_lock<std::mutex> lck(_mutex);
			_busyThreadCount += 1;
		}

		//---------- Running the task. -----------------------
		try{
			task->processTask(mySQL);
		} catch (...) {}

		task.reset();

		{
			std::unique_lock<std::mutex> lck(_mutex);
			_busyThreadCount -= 1;
		}
	}
}

void MySQLTaskThreadPool::temporaryProcess()
{
	MySQLClient *mySQL = new MySQLClient(_dbInfo->host, _dbInfo->port, _dbInfo->username,
		_dbInfo->password, _dbInfo->databaseName, _dbInfo->timeout);

	AutoDeleteGuard<MySQLClient> adg(mySQL);

	int64_t latencyStartTime = 0;
	int64_t restLatencySeconds = _tempThreadLatencySeconds;

	while (true)
	{
		std::shared_ptr<TaskPackage> task;
		while (true)
		{
			task = _taskQueue->pop();
			if (task)
				break;

			std::unique_lock<std::mutex> lck(_mutex);
			if (restLatencySeconds <= 0 || _willExit)
			{
				_tempThreadCount -= 1;
				_detachCondition.notify_one();
				return;
			}

			latencyStartTime = slack_mono_sec();
			_condition.wait_for(lck, std::chrono::seconds(restLatencySeconds));
			restLatencySeconds -= slack_mono_sec() - latencyStartTime;
		}
		
		{
			restLatencySeconds = _tempThreadLatencySeconds;
			std::unique_lock<std::mutex> lck(_mutex);
			_busyThreadCount += 1;
		}

		//---------- Running the task. -----------------------
		try{
			task->processTask(mySQL);
		} catch (...) {}

		task.reset();

		{
			std::unique_lock<std::mutex> lck(_mutex);
			_busyThreadCount -= 1;
		}
	}
}

/*===========================================================================

FUNCTION: ThreadPool::GetCounter

DESCRIPTION:
  Get the thread status counters.

PARAMETERS:
	ulNormalNum    [in] - The number of threads held by pool. (Not include the number of temporary threads.)
	ulTemporaryNum [in] - The number of temporary threads held by pool.
	ulBusyNum      [in] - The number of threads working on tasks. (Not include the number of temporary threads.)
	nTaskWaitDeal  [in] - The number of tasks waiting to be dealt.

RETURN VALUE:
   NUone.
===========================================================================*/
void MySQLTaskThreadPool::status(int32_t &normalThreadCount, int32_t &temporaryThreadCount, int32_t &busyThreadCount, int32_t& min, int32_t& max)
{
	if (_inited)
	{
		std::lock_guard<std::mutex> lck (_mutex);
		normalThreadCount = _normalThreadCount;
		temporaryThreadCount = _tempThreadCount;
		busyThreadCount = _busyThreadCount;
		min = _initCount;
		max = _maxCount;
	}
	else
	{
		normalThreadCount = 0;
		temporaryThreadCount = 0;
		busyThreadCount = 0;
		min = 0;
		max = 0;
	}
}

std::string MySQLTaskThreadPool::infos()
{
    int32_t min = 0, max = 0;
    int32_t normalThreadCount = 0;
    int32_t temporaryThreadCount = 0;
    int32_t busyThreadCount = 0;

	status(normalThreadCount, temporaryThreadCount, busyThreadCount, min, max);

	std::ostringstream oss;
	oss<<"\"totalThreads\":"<<(normalThreadCount + temporaryThreadCount);
	oss<<",\"normalThreads\":"<<normalThreadCount;
	oss<<",\"busyThreads\":"<<busyThreadCount;
	oss<<",\"temporaryThreads\":"<<temporaryThreadCount;
	oss<<",\"min\":"<<min;
	oss<<",\"max\":"<<max;

    return oss.str();
}

bool MySQLTaskThreadPool::isBusy()
{
	std::lock_guard<std::mutex> lck (_mutex);
	return ((_busyThreadCount + _tempThreadCount) > 0);
}

/*===========================================================================

FUNCTION: ThreadPool::Free

DESCRIPTION:
  Free the thread pool.

PARAMETERS:
	None.

RETURN VALUE:
   None.
===========================================================================*/
void MySQLTaskThreadPool::release()
{
	if (!_inited)
		return;

	{
		std::unique_lock<std::mutex> lck(_mutex);
		_willExit = true;
		_condition.notify_all();
	}

	for (auto& th: _threadList)
		th.join();

	std::unique_lock<std::mutex> lck (_mutex);
	while (_tempThreadCount)
		 _detachCondition.wait(lck);

	_inited = false;
}
