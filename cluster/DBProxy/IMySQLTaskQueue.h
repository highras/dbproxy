#ifndef MySQL_Task_Queue_Interface_H
#define MySQL_Task_Queue_Interface_H

class TaskPackage;

class IMySQLTaskQueue
	{
	public:
		virtual ~IMySQLTaskQueue() {}
		
		virtual bool empty() = 0;
		virtual size_t size() = 0;
		virtual void clear() = 0;

		virtual std::shared_ptr<TaskPackage> pop() throw () = 0;
	};

#endif
