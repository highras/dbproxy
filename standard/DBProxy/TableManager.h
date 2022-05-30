#ifndef TableManager_h_
#define TableManager_h_

#include <set>
#include <map>
#include <unordered_map>
#include <memory>
#include <string>
#include <vector>
#include "MySQLTaskThreadPool.h"
#include "TaskQueue.h"

struct DatabaseInfo
{
	int serverId;
	std::string host;
	int port;
	std::string databaseName;
	std::string username;
	std::string password;
	int timeout;
	int master_id;
	
private:
	MySQLTaskThreadPool* _threadPool;
	
public:
	DatabaseInfo();
	~DatabaseInfo();
	
	inline bool wakeUp() { return (_threadPool ? _threadPool->wakeUp() : false); }
	inline bool threadPoolBusy() { return (_threadPool ? _threadPool->isBusy() : false); }
	void enableThreadPool(IMySQLTaskQueue* taskQueue, int32_t initCount, int32_t perAppendCount, int32_t perfectCount, int32_t maxCount, size_t tempThreadLatencySeconds);
	
	std::string threadPoolInfos();
	bool operator == (const DatabaseInfo &r) const		//-- equivalent function.
	{
		//-- Basic only: (host == r.host && port == r.port).
		return (host == r.host && port == r.port && username == r.username && password == r.password && timeout == r.timeout);
	}
};
typedef std::shared_ptr<DatabaseInfo> DatabaseInfoPtr;

struct TableInfo
{
	std::string tableName;
	bool splitByRange;

	//---- for range split -----
	int64_t splitSpan;
	std::string databaseCategory;
	bool secondarySplit;
	int64_t seconddarySplitSpan;

	//---- for hash split ------
	int tableCount;
	std::string splitHint;
};

struct TableSplittingInfo
{
	std::string tableName;
	int tableNumber;
	int serverId;
	std::string databaseName;
};

struct TableTaskHint
{
	std::string databaseName;
	std::string tableName;
	int tableNumber;
	
	TableTaskHint(): databaseName(), tableName(), tableNumber(0) {}
	TableTaskHint(const std::string& tableName_, int table_number): databaseName(), tableName(tableName_), tableNumber(table_number) {}
	TableTaskHint(const std::string& databaseName_, const std::string& tableName_, int table_number): databaseName(databaseName_), tableName(tableName_), tableNumber(table_number) {}
	TableTaskHint(const TableTaskHint &r): databaseName(r.databaseName), tableName(r.tableName), tableNumber(r.tableNumber) {}
	TableTaskHint(const TableSplittingInfo &r): databaseName(r.databaseName), tableName(r.tableName), tableNumber(r.tableNumber) {}
	
	bool operator == (const TableTaskHint &r) const
	{
		return (tableNumber == r.tableNumber && tableName == r.tableName);
	}
	bool operator != (const TableTaskHint &r) const
	{
		return (tableNumber != r.tableNumber || tableName != r.tableName);
	}
	bool operator >= (const TableTaskHint &r) const
	{
		if (tableNumber > r.tableNumber)
			return true;
		else if (tableNumber < r.tableNumber)
			return false;
		else
			return tableName >= r.tableName;
	}
	bool operator <= (const TableTaskHint &r) const
	{
		if (tableNumber < r.tableNumber)
			return true;
		else if (tableNumber > r.tableNumber)
			return false;
		else
			return tableName <= r.tableName;
	}
	bool operator > (const TableTaskHint &r) const
	{
		if (tableNumber > r.tableNumber)
			return true;
		else if (tableNumber < r.tableNumber)
			return false;
		else
			return tableName > r.tableName;
	}
	bool operator < (const TableTaskHint &r) const
	{
		if (tableNumber < r.tableNumber)
			return true;
		else if (tableNumber > r.tableNumber)
			return false;
		else
			return tableName < r.tableName;
	}
};

struct DatabaseTaskQueue
{
	bool inited;
	RWTaskQueue	queue;
	DatabaseInfoPtr masterDB;	//-- masterDB also in databaseList.
	std::vector<DatabaseInfoPtr> databaseList;
	
	DatabaseTaskQueue(): inited(false) {}
	~DatabaseTaskQueue()
	{
		masterDB.reset();
		databaseList.clear();
	}

	bool deletable()
	{
		if (queue.size() > 0)
			return false;

		for (auto& dbiPtr: databaseList)
			if (dbiPtr->threadPoolBusy())
				return false;

		return true;
	}

	bool operator == (const DatabaseTaskQueue &r) const		//-- equivalent function.
	{
		if (databaseList.size() != r.databaseList.size())
			return false;

		if (!(*masterDB == *r.masterDB))
			return false;

		for (auto dip :databaseList)
		{
			bool findEquivalent = false;
			for (auto rdip: r.databaseList)
			{
				if (*dip == *rdip)
				{
					findEquivalent = true;
					break;
				}
			}

			if (!findEquivalent)
				return false;
		}
		return true;
	}
};
typedef std::shared_ptr<DatabaseTaskQueue> DatabaseTaskQueuePtr;

struct RangedDatabaseTaskQueue
{
	std::shared_ptr<DatabaseTaskQueue> databaseTaskQueue;
	std::string databaseName;
};

struct RangeDatabaseNode
{
	bool oddEvened;
	RangedDatabaseTaskQueue* arr[2];

	RangeDatabaseNode(): oddEvened(false) { arr[0] = NULL; arr[1] = NULL; }
	~RangeDatabaseNode()
	{
		if (arr[0])
			delete arr[0];
		if (arr[1])
			delete arr[1];
	}
};

struct SplitInfo
{
	bool splitByRange;

	//---- for range split -----
	int64_t splitSpan;
	int rangeCount;
	std::string databaseCategory;
	bool secondarySplit;
	int64_t seconddarySplitSpan;

	//---- for hash split ------
	int tableCount;
	std::string splitHint;
};

struct DatabaseCategoryInfo
{
	int totalCount;
	std::vector<int> oddEvenIndexes;
};

class TableManager
{	
	int64_t _splitSpan;
	int _secondaryTableNumberBase;
	int64_t _update_time;

	static size_t _perThreadPoolReadQueueMaxLength;
	static size_t _perThreadPoolWriteQueueMaxLength;
	
	std::unordered_map<std::string, TableInfo*>	_tableInfos;
	std::map<TableTaskHint, DatabaseTaskQueuePtr> _tableTaskQueues;		//-- for hash
	std::unordered_map<std::string, std::vector<RangeDatabaseNode*>> _rangedTaskQueues;
	std::set<DatabaseTaskQueuePtr> _usedTaskQueues;
	std::set<DatabaseTaskQueuePtr> _takenTaskQueues;

	friend class TableManagerBuilder;
	DatabaseTaskQueuePtr findDatabaseTaskQueue(TaskPackagePtr task, int64_t hintId,
		const std::string& tableName, std::string& sql, std::string* databaseName);
	
public:
	TableManager(int64_t range_span, int secondary_split_table_number_base, int64_t update_time);
	~TableManager();
	
	inline int64_t updateTime() { return _update_time; }
	void signTakenOverTaskQueues(TableManager& newTableManager);
	bool deletable();	//-- just used for check deletable when stop push new task.
	bool splitType(const std::string &table_name, bool& splitByRange);	//-- using internal.
	
	bool query(int64_t hintId, bool master, QueryTaskPtr task);
	bool splitInfo(const std::string &table_name, SplitInfo& info);
	bool databaseCategoryInfo(const std::string& databaseCategory, DatabaseCategoryInfo& info);
	bool getAllSplitTablesHintIds(const std::string &table_name, std::set<int64_t>& hintIds);
	bool reformHintIds(const std::string &table_name, const std::vector<int64_t>& hintIds,
		std::map<int64_t, std::set<int64_t>>& hintMap, std::set<int64_t>& invalidHintIds);
	
	bool transaction(TransactionTaskPtr task);

	std::string statusInJSON();
	static void config(int perThreadPoolReadQueueMaxLength, int perThreadPoolWriteQueueMaxLength);
};
typedef std::shared_ptr<TableManager> TableManagerPtr;

#endif
