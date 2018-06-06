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

struct DatabaseInfo		//-- Mapping to server_info table in database.
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

struct TableInfo		//-- Mapping to table_info table in database.
{
	std::string tableName;
	std::string cluster;
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

struct TableSplittingInfo		//-- Mapping to split_table_info table in database.
{
	std::string tableName;
	int tableNumber;
	int serverId;
	std::string cluster;
	std::string databaseName;
};

struct RangeSplittingInfo		//-- Mapping to split_range_info table in database.
{
	int index;
	int indexType;
	int serverId;
	std::string databaseCategory;
	std::string databaseName;
	std::string cluster;
};

struct TableTaskHint
{
	std::string databaseName;
	std::string tableName;
	std::string cluster;
	int tableNumber;
	
	TableTaskHint(): databaseName(), tableName(), cluster(), tableNumber(0) {}
	TableTaskHint(const std::string& tableName_, const std::string& clusterName, int table_number):
		databaseName(), tableName(tableName_), cluster(clusterName), tableNumber(table_number) {}
	TableTaskHint(const std::string& databaseName_, const std::string& tableName_, const std::string& clusterName, int table_number):
		databaseName(databaseName_), tableName(tableName_), cluster(clusterName), tableNumber(table_number) {}
	TableTaskHint(const TableTaskHint &r): databaseName(r.databaseName), tableName(r.tableName), cluster(r.cluster), tableNumber(r.tableNumber) {}
	TableTaskHint(const TableSplittingInfo &r): databaseName(r.databaseName), tableName(r.tableName), cluster(r.cluster), tableNumber(r.tableNumber) {}
	
	bool operator == (const TableTaskHint &r) const
	{
		return (tableNumber == r.tableNumber && tableName == r.tableName && cluster == r.cluster);
	}
};

struct TableHint
{
	std::string tableName;
	std::string cluster;

	TableHint(): tableName(), cluster() {}
	TableHint(const std::string& tableName_, const std::string& cluster_): tableName(tableName_), cluster(cluster_) {}
	TableHint(const TableHint& info): tableName(info.tableName), cluster(info.cluster) {}
	TableHint(const TableInfo& info): tableName(info.tableName), cluster(info.cluster) {}

	bool operator == (const TableHint &r) const
	{
		return tableName == r.tableName && cluster == r.cluster;
	}
};

struct RangeTaskHint
{
	std::string databaseCategory;
	std::string cluster;

	RangeTaskHint(): databaseCategory(), cluster() {}
	RangeTaskHint(const std::string& dbCategory, const std::string& clusterName): databaseCategory(dbCategory), cluster(clusterName) {}
	RangeTaskHint(const TableInfo& info): databaseCategory(info.databaseCategory), cluster(info.cluster) {}
	RangeTaskHint(const RangeTaskHint& info): databaseCategory(info.databaseCategory), cluster(info.cluster) {}
	RangeTaskHint(const RangeSplittingInfo& info): databaseCategory(info.databaseCategory), cluster(info.cluster) {}

	bool operator == (const RangeTaskHint &r) const
	{
		return databaseCategory == r.databaseCategory && cluster == r.cluster;
	}
};

template <class T>
inline void hash_combine(std::size_t& seed, const T& v)		//-- copy from boost library.
{
	std::hash<T> hasher;
	seed ^= hasher(v) + 0x9e3779b9 + (seed<<6) + (seed>>2);
}

template <class T>
inline void hash_combine(std::hash<T>& hasher, std::size_t& seed, const T& v)
{
	seed ^= hasher(v) + 0x9e3779b9 + (seed<<6) + (seed>>2);
}

namespace std
{
	template<> struct hash<TableTaskHint>
	{
		std::size_t operator()(const TableTaskHint &tth) const
		{
			std::size_t seed = 0;
			std::hash<std::string> hasher;

			hash_combine<std::string>(hasher, seed, tth.tableName);
			hash_combine<std::string>(hasher, seed, tth.cluster);
			hash_combine<int>(seed, tth.tableNumber);

			return seed;
		}
	};

	template<> struct hash<TableHint>
	{
		std::size_t operator()(const TableHint &th) const
		{
			std::size_t seed = 0;
			std::hash<std::string> hasher;

			hash_combine(hasher, seed, th.tableName);
			hash_combine(hasher, seed, th.cluster);

			return seed;
		}
	};

	template<> struct hash<RangeTaskHint>
	{
		std::size_t operator()(const RangeTaskHint &th) const
		{
			std::size_t seed = 0;
			std::hash<std::string> hasher;

			hash_combine(hasher, seed, th.databaseCategory);
			hash_combine(hasher, seed, th.cluster);

			return seed;
		}
	};
}

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
	
	std::unordered_map<TableHint, TableInfo*>	_tableInfos;
	std::unordered_map<TableTaskHint, DatabaseTaskQueuePtr> _tableTaskQueues;		//-- for hash
	std::unordered_map<RangeTaskHint, std::vector<RangeDatabaseNode*>> _rangedTaskQueues;
	std::set<DatabaseTaskQueuePtr> _usedTaskQueues;
	std::set<DatabaseTaskQueuePtr> _takenTaskQueues;

	friend class TableManagerBuilder;
	DatabaseTaskQueuePtr findDatabaseTaskQueue(TaskPackagePtr task, int64_t hintId,
		const std::string& tableName, const std::string& cluster, std::string& sql, std::string* databaseName);
	
public:
	TableManager(int64_t range_span, int secondary_split_table_number_base, int64_t update_time);
	~TableManager();
	
	inline int64_t updateTime() { return _update_time; }
	void signTakenOverTaskQueues(TableManager& newTableManager);
	bool deletable();	//-- just used for check deletable when stop push new task.
	bool splitType(const std::string &table_name, const std::string& cluster, bool& splitByRange);	//-- using internal.
	
	bool query(int64_t hintId, bool master, QueryTaskPtr task);
	bool splitInfo(const std::string &table_name, const std::string& cluster, SplitInfo& info);
	bool databaseCategoryInfo(const std::string& databaseCategory, const std::string& cluster, DatabaseCategoryInfo& info);
	bool getAllSplitTablesHintIds(const std::string &table_name, const std::string& cluster, std::set<int64_t>& hintIds);
	bool reformHintIds(const std::string &table_name, const std::string& cluster, const std::vector<int64_t>& hintIds,
		std::map<int64_t, std::set<int64_t>>& hintMap, std::set<int64_t>& invalidHintIds);
	
	bool transaction(TransactionTaskPtr task);

	std::string statusInJSON();
	static void config(int perThreadPoolReadQueueMaxLength, int perThreadPoolWriteQueueMaxLength);
};
typedef std::shared_ptr<TableManager> TableManagerPtr;

#endif
