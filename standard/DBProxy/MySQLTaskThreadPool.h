#ifndef MySQL_Task_Thread_Pool_H
#define MySQL_Task_Thread_Pool_H

/*===============================================================================
  INCLUDES AND VARIABLE DEFINITIONS
  =============================================================================== */
#include <mutex>
#include <list>
#include <memory>
#include <thread>
#include <condition_variable>
#include "PoolInfo.h"
#include "IMySQLTaskQueue.h"
/*===============================================================================
  CLASS & STRUCTURE DEFINITIONS
  =============================================================================== */
struct DatabaseInfo;

class MySQLTaskThreadPool
{
	private:
		std::mutex _mutex;
		std::condition_variable _condition;
		std::condition_variable _detachCondition;

		int32_t					_initCount;
		int32_t					_appendCount;
		int32_t					_perfectCount;
		int32_t					_maxCount;
		size_t					_tempThreadLatencySeconds;

		int32_t					_normalThreadCount;		//-- The number of normal work threads in pool.
		int32_t					_busyThreadCount;		//-- The number of work threads which are busy for processing.
		int32_t					_tempThreadCount;		//-- The number of temporary/overdraft work threads.

		IMySQLTaskQueue*		_taskQueue;
		std::list<std::thread>	_threadList;

		bool					_inited;
		bool					_willExit;

		DatabaseInfo*			_dbInfo;

		void					ReviseDataRelation();
		bool					append();
		void					process();
		void					temporaryProcess();

	public:
		bool					init(int32_t initCount, int32_t perAppendCount, int32_t perfectCount, int32_t maxCount, size_t tempThreadLatencySeconds);
		bool					wakeUp();
		bool					isBusy();
		void					release();
		void					status(int32_t &normalThreadCount, int32_t &temporaryThreadCount, int32_t &busyThreadCount, int32_t& min, int32_t& max);
		std::string				infos();

		inline bool inited()
		{
			return _inited;
		}

		inline bool exiting()
		{
			return _willExit;
		}

		MySQLTaskThreadPool(IMySQLTaskQueue *taskQueue, DatabaseInfo* dbInfo):
			_initCount(0), _appendCount(0), _perfectCount(0), _maxCount(0), _tempThreadLatencySeconds(0),
			_normalThreadCount(0), _busyThreadCount(0), _tempThreadCount(0),
			_taskQueue(taskQueue), _inited(false), _willExit(false), _dbInfo(dbInfo)
		{
		}

		~MySQLTaskThreadPool()
		{
			release();
		}
};
#endif
