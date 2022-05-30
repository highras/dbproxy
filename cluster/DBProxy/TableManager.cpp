#include <algorithm>
#include <sstream>
#include "FPLog.h"
#include "SQLParser.h"
#include "DataRouterErrorInfo.h"
#include "TableManager.h"
//=============================================//
//-	DatabaseInfo
//=============================================//
DatabaseInfo::DatabaseInfo(): port(0), master_id(0), _threadPool(0)
{
}

DatabaseInfo::~DatabaseInfo()
{
	if (_threadPool)
		delete _threadPool;
}

void DatabaseInfo::enableThreadPool(IMySQLTaskQueue* taskQueue, int32_t initCount, int32_t perAppendCount, int32_t perfectCount, int32_t maxCount, size_t tempThreadLatencySeconds)
{
	if (!_threadPool)
	{
		_threadPool = new MySQLTaskThreadPool(taskQueue, this);
		_threadPool->init(initCount, perAppendCount, perfectCount, maxCount, tempThreadLatencySeconds);
	}
}

std::string DatabaseInfo::threadPoolInfos()
{
	if (_threadPool)
		return _threadPool->infos();
	else
		return std::string();
}

//=============================================//
//-	TableManager
//=============================================//
size_t TableManager::_perThreadPoolReadQueueMaxLength = 200000;
size_t TableManager::_perThreadPoolWriteQueueMaxLength = 200000;

void TableManager::config(int perThreadPoolReadQueueMaxLength, int perThreadPoolWriteQueueMaxLength)
{
	_perThreadPoolReadQueueMaxLength = (size_t)perThreadPoolReadQueueMaxLength;
	_perThreadPoolWriteQueueMaxLength = (size_t)perThreadPoolWriteQueueMaxLength;
}

TableManager::TableManager(int64_t range_span, int secondary_split_table_number_base, int64_t update_time):
	_splitSpan(range_span), _secondaryTableNumberBase(secondary_split_table_number_base), _update_time(update_time)
{
}

TableManager::~TableManager()
{	
	for (auto& tiPair: _tableInfos)
		delete tiPair.second;

	for (auto& rtqPair: _rangedTaskQueues)
	{
		for (RangeDatabaseNode* rdn: rtqPair.second)
			delete rdn;
	}
}

void TableManager::signTakenOverTaskQueues(TableManager& newTableManager)
{
	for (auto taskQueue: _usedTaskQueues)
	{
		for (auto newTaskQueue: newTableManager._usedTaskQueues)
		{
			if (taskQueue == newTaskQueue)
			{
				_takenTaskQueues.insert(taskQueue);
				break;
			}
		}	
	}
}
bool TableManager::deletable()
{
	for (auto& dbQueuePtr: _usedTaskQueues)
	{
		if (_takenTaskQueues.find(dbQueuePtr) != _takenTaskQueues.end())
			continue;
		
		if (!dbQueuePtr->deletable())
			return false;
	}
	
	return true;
}

DatabaseTaskQueuePtr TableManager::findDatabaseTaskQueue(TaskPackagePtr task, int64_t hintId,
	const std::string& tableName, const std::string& cluster, std::string& sql, std::string* databaseName)
{
	TableHint tableHint(tableName, cluster);

	auto iter = _tableInfos.find(tableHint);
	if (iter == _tableInfos.end())
	{
		LOG_ERROR("EXCEPTION: Table '%s' in cluster '%s' not found.", tableName.c_str(), cluster.c_str());
		return nullptr;
	}
	TableInfo* tableInfo = iter->second;
	
	DatabaseTaskQueuePtr databaseQueuePtr;
	if (tableInfo->splitByRange)
	{
		RangeTaskHint rth(*tableInfo);
		auto rtqIter = _rangedTaskQueues.find(rth);
		if (rtqIter != _rangedTaskQueues.end())
		{
			std::vector<RangeDatabaseNode*>& vec = rtqIter->second;

			int64_t splitSpan = tableInfo->splitSpan;
			size_t index = 0;
			if (splitSpan)
			{
				if (hintId < 0)
				{
					LOG_ERROR("EXCEPTION: Hint id of ranged split query is negative: Table '%s' in cluster '%s' hintId: %lld",
						tableName.c_str(), cluster.c_str(), hintId);
					return nullptr;
				}
		
				index = (size_t)(hintId / splitSpan);
			}

			if (index < vec.size())
			{
				RangeDatabaseNode* node = vec[index];
				RangedDatabaseTaskQueue* rangeQueue = NULL;

				if (node->oddEvened)
					rangeQueue = (hintId & 0x1) ? node->arr[0] : node->arr[1];
				else
					rangeQueue = node->arr[0];

				if (rangeQueue)
				{
					databaseQueuePtr = rangeQueue->databaseTaskQueue;
					if (task)
						task->setDatabaseName(rangeQueue->databaseName);
					else
						*databaseName = rangeQueue->databaseName;

					if (tableInfo->secondarySplit)
					{
						int64_t suffixId = hintId - index * splitSpan;
						suffixId /= tableInfo->seconddarySplitSpan;

						char suffix[32];
#ifdef __APPLE__
						snprintf(suffix, 32, "%lld", suffixId + _secondaryTableNumberBase);
#else
						snprintf(suffix, 32, "%ld", suffixId + _secondaryTableNumberBase);
#endif
						
						if (!TaskPackage::setSuffix(tableName, sql, suffix))
						{
							LOG_ERROR("Parse SQL [%s] failed. Cannot find the table name [%s] in sql.", sql.c_str(), tableName.c_str());
							return nullptr;
						}
					}
				}//-- else can be happend. Pls refer the RangedDatabaseTaskQueue check in tableManagerBuilder.
			}
			else
			{
				LOG_ERROR("EXCEPTION: Ranged split query: Table '%s' in cluster '%s' reuqire sub db/table index %u, but current only %u sub db/tables. "
					"hintId: %lld, split span: %lld", tableName.c_str(), cluster.c_str(), index, vec.size(), hintId, splitSpan);
			}
		}
		else
			LOG_ERROR("EXCEPTION: DB category '%s' cluster '%s' for table '%s' is not found.",
				tableInfo->databaseCategory.c_str(), cluster.c_str(), tableName.c_str());
	}
	else
	{
		int count = tableInfo->tableCount;
		if (count <= 1)
			hintId = 0;
		else
		{
			if (hintId < 0)
				hintId = -hintId;

			if (hintId >= count)
				hintId = hintId % count;
		}

		TableTaskHint tth(tableInfo->tableName, cluster, (int)hintId);
		auto it = _tableTaskQueues.find(tth);
		if (it != _tableTaskQueues.end())
		{
			databaseQueuePtr = it->second;

			if (count > 1)
			{
				char suffix[32];
#ifdef __APPLE__
				snprintf(suffix, 32, "_%lld", hintId);
#else
				snprintf(suffix, 32, "_%ld", hintId);
#endif
				
				if (!TaskPackage::setSuffix(tableName, sql, suffix))
				{
					LOG_ERROR("Parse SQL [%s] failed. Cannot find the table name [%s] in sql.", sql.c_str(), tableName.c_str());
					return nullptr;
				}
			}

			if (task)
				task->setDatabaseName(it->first.databaseName);
			else
				*databaseName = it->first.databaseName;
		}
	}

	return databaseQueuePtr;
}

bool TableManager::query(int64_t hintId, bool master, QueryTaskPtr task)
{
	DatabaseTaskQueuePtr databaseQueuePtr = findDatabaseTaskQueue(task, hintId, task->tableName(), task->cluster(), task->sql(), NULL);

	if (databaseQueuePtr == nullptr)
	{
		task->finish(ErrorInfo::notFoundCode, "Target database or table not found.");
		LOG_ERROR("EXCEPTION: Database or table not found. Table: %s, cluster: %s, hintId: %lld.",
			task->tableName().c_str(), task->cluster().c_str(), hintId);
		return false;
	}
		
	if (!master && databaseQueuePtr->databaseList.size() > 1)
	{
		if (databaseQueuePtr->queue.readQueueSize() >= _perThreadPoolReadQueueMaxLength)
		{
			task->finish(ErrorInfo::serverBusyCode, "Corresponding query queue caught limitation.");
			return false;
		}

		databaseQueuePtr->queue.push(task, true);
		size_t v = ((uint64_t)task.get()/16) % databaseQueuePtr->databaseList.size();
		
		for (size_t i = 0; i < databaseQueuePtr->databaseList.size(); i++)
		{
			DatabaseInfoPtr dip = databaseQueuePtr->databaseList[v];
			if (dip->wakeUp())
				return true;
				
			v++;
			if (v == databaseQueuePtr->databaseList.size())
				v = 0;
		}
		return false;
	}
	else
	{
		if (databaseQueuePtr->queue.writeQueueSize() >= _perThreadPoolWriteQueueMaxLength)
		{
			task->finish(ErrorInfo::serverBusyCode, "Corresponding query queue caught limitation.");
			return false;
		}

		databaseQueuePtr->queue.push(task, false);
		return databaseQueuePtr->masterDB->wakeUp();
	}
}

bool TableManager::splitType(const std::string &table_name, const std::string& cluster, bool& splitByRange)
{
	TableHint hint(table_name, cluster);
	auto iter = _tableInfos.find(hint);
	if (iter == _tableInfos.end())
		return false;

	splitByRange = iter->second->splitByRange;
	return true;
}
bool TableManager::splitInfo(const std::string &table_name, const std::string& cluster, SplitInfo& info)
{
	TableHint tableHint(table_name, cluster);
	auto iter = _tableInfos.find(tableHint);
	if (iter == _tableInfos.end())
		return false;

	TableInfo* tableInfo = iter->second;

	info.splitByRange = tableInfo->splitByRange;
	if (tableInfo->splitByRange)
	{
		RangeTaskHint rangeHint(*tableInfo);
		auto rangeTaskQueueIter = _rangedTaskQueues.find(rangeHint);
		if (rangeTaskQueueIter == _rangedTaskQueues.end())
			return false;
		else
			info.rangeCount = (int)rangeTaskQueueIter->second.size();
	}
	else
		info.rangeCount = 0;

	info.splitSpan = tableInfo->splitSpan;
	info.databaseCategory = tableInfo->databaseCategory;
	info.secondarySplit = tableInfo->secondarySplit;
	info.seconddarySplitSpan = tableInfo->seconddarySplitSpan;

	info.tableCount = tableInfo->tableCount;
	info.splitHint = tableInfo->splitHint;

	return true;
}

bool TableManager::databaseCategoryInfo(const std::string& databaseCategory, const std::string& cluster, DatabaseCategoryInfo& info)
{
	RangeTaskHint rangeHint(databaseCategory, cluster);
	auto iter = _rangedTaskQueues.find(rangeHint);
	if (iter == _rangedTaskQueues.end())
		return false;

	std::vector<RangeDatabaseNode*>& vec = iter->second;
	info.totalCount = (int)vec.size();
	for (int i = 0; i < info.totalCount; i++)
	{
		if (vec[i]->oddEvened)
			info.oddEvenIndexes.push_back(i);
	}
	return true;
}

bool TableManager::getAllSplitTablesHintIds(const std::string &table_name, const std::string& cluster, std::set<int64_t>& hintIds)
{
	TableHint tableHint(table_name, cluster);
	auto iter = _tableInfos.find(tableHint);
	if (iter == _tableInfos.end())
		return false;

	TableInfo* tableInfo = iter->second;

	if (tableInfo->splitByRange)
	{
		if (tableInfo->splitSpan == 0)
		{
			hintIds.insert(0);
			return true;
		}

		RangeTaskHint rangeHint(*tableInfo);
		auto rangeTaskQueueIter = _rangedTaskQueues.find(rangeHint);
		if (rangeTaskQueueIter == _rangedTaskQueues.end())
			return false;
		
		std::vector<RangeDatabaseNode*>& vec = rangeTaskQueueIter->second;
		int rangeCount = (int)vec.size();

		if (!tableInfo->secondarySplit)
		{
			for (int i = 0; i < rangeCount; i++)
			{
				int64_t newId = i * tableInfo->splitSpan;
				hintIds.insert(newId);

				if (vec[i]->oddEvened)
					hintIds.insert(newId + 1);
			}
		}
		else
		{
			int secondaryCount = tableInfo->splitSpan / tableInfo->seconddarySplitSpan;
			if (secondaryCount * tableInfo->seconddarySplitSpan < tableInfo->splitSpan)
				secondaryCount += 1;

			for (int i = 0; i < rangeCount; i++)
			{
				int64_t firstId = i * tableInfo->splitSpan;
				for (int64_t j = 0; j < secondaryCount; j++)
				{
					int64_t newId = firstId + j * tableInfo->seconddarySplitSpan;
					hintIds.insert(newId);

					if (vec[i]->oddEvened)
						hintIds.insert(newId + 1);
				}
			}
		}
	}
	else
	{
		hintIds.insert(0);
		for (int i = 1; i < tableInfo->tableCount; i++)
			hintIds.insert(i);
	}
	return true;
}

void addIdToMap(int64_t key, int64_t id, std::map<int64_t, std::set<int64_t>>& hintMap)
{
	auto iter = hintMap.find(key);
	if (iter != hintMap.end())
		iter->second.insert(id);
	else
	{
		std::set<int64_t> s;
		s.insert(id);
		hintMap[key] = s;
	}
}

bool TableManager::reformHintIds(const std::string &table_name, const std::string& cluster, const std::vector<int64_t>& hintIds,
	std::map<int64_t, std::set<int64_t>>& hintMap, std::set<int64_t>& invalidHintIds)
{
	TableHint tableHint(table_name, cluster);
	auto iter = _tableInfos.find(tableHint);
	if (iter == _tableInfos.end())
		return false;

	TableInfo* tableInfo = iter->second;

	std::set<int64_t> positiveHintIds;
	for (int64_t hintId: hintIds)
	{
		if (hintId < 0)
		{
			if (tableInfo->splitByRange)
				invalidHintIds.insert(hintId);
			else
				positiveHintIds.insert(-hintId);
		}
		else
			positiveHintIds.insert(hintId);
	}

	if (tableInfo->splitByRange)
	{
		if (tableInfo->splitSpan == 0)
		{
			hintMap[0].swap(positiveHintIds);
			return true;
		}

		RangeTaskHint rangeHint(*tableInfo);
		auto rangeTaskQueueIter = _rangedTaskQueues.find(rangeHint);
		if (rangeTaskQueueIter == _rangedTaskQueues.end())
			return false;
		
		std::vector<RangeDatabaseNode*>& vec = rangeTaskQueueIter->second;
		int rangeCount = (int)vec.size();

		for (int64_t oldId: positiveHintIds)
		{
			int idx = oldId / tableInfo->splitSpan;
			if (idx >= rangeCount)
			{
				invalidHintIds.insert(oldId);
				continue;
			}

			int amendatory = 0;
			if (tableInfo->secondarySplit)
			{
				int secondaryIdx = (oldId - idx * tableInfo->splitSpan) / tableInfo->seconddarySplitSpan;
				amendatory = secondaryIdx * tableInfo->seconddarySplitSpan;
			}
	
			int64_t newId = idx * tableInfo->splitSpan + amendatory;
			if (vec[idx]->oddEvened)
			{
				if ((oldId ^ newId) & 0x1)
					newId += 1;
			}

			addIdToMap(newId, oldId, hintMap);
		}
	}
	else
	{
		for (int64_t oldId: positiveHintIds)
		{
			int64_t newId = oldId % tableInfo->tableCount;
			addIdToMap(newId, oldId, hintMap);
		}
	}

	return true;
}

bool TableManager::transaction(TransactionTaskPtr task)
{
	std::string databaseName;
	DatabaseTaskQueuePtr dbTaskQueue;

	for (size_t i = 0; i < task->_sqls.size(); i++)
	{
		std::string currentDatabaseName;
		DatabaseTaskQueuePtr taskQueue = findDatabaseTaskQueue(nullptr, task->_hintIds[i],
			task->_tableNames[i], task->cluster(), task->_sqls[i], &currentDatabaseName);

		if (!taskQueue)
		{
			task->finish(ErrorInfo::notFoundCode, i, "Target database or table not found.");
			return false;
		}

		if (dbTaskQueue)
		{
			if (dbTaskQueue.get() != taskQueue.get() || databaseName != currentDatabaseName)
			{
				task->finish(ErrorInfo::disabledCode, i, "Target database or database instance is different from previous.");
				return false;
			}
		}
		else
		{
			databaseName = currentDatabaseName;
			dbTaskQueue = taskQueue;
		}
	}

	if (dbTaskQueue == nullptr)
	{
		task->finish(ErrorInfo::disabledCode, "Invalid transaction.");
		return false;
	}

	if (dbTaskQueue->queue.writeQueueSize() >= _perThreadPoolWriteQueueMaxLength)
	{
		task->finish(ErrorInfo::serverBusyCode, "Corresponding query queue caught limitation.");
		return false;
	}

	task->setDatabaseName(databaseName);
	dbTaskQueue->queue.push(task, false);
	return dbTaskQueue->masterDB->wakeUp();
}

std::string TableManager::statusInJSON()
{	
	std::ostringstream oss;
	bool comma = false;
	
	oss<<"{\"configUpdateTime\":"<<_update_time<<",";
	oss<<"\"dbGroupInfos\":[";
	
	for (std::shared_ptr<DatabaseTaskQueue> dtqp: _usedTaskQueues)
	{
		if (comma)
			oss<<",";
		else
			comma = true;
			
		oss<<"{";
			oss<<"\"masterDB\":\""<<dtqp->masterDB->host<<":"<<dtqp->masterDB->port<<"\"";
			oss<<",\"readQueueSize\":"<<dtqp->queue.readQueueSize();
			oss<<",\"writeQueueSize\":"<<dtqp->queue.writeQueueSize();
		
			oss<<",\"dbInfos\":";
			oss<<"[";
			
			bool comma2 = false;
			for (DatabaseInfoPtr dip: dtqp->databaseList)
			{
				if (comma2)
					oss<<",";
				else
					comma2 = true;
					
				oss<<"{\"dbHost\":\""<<dip->host<<":"<<dip->port<<"\"";
				oss<<","<<dip->threadPoolInfos();
				oss<<"}";
			}
			oss<<"]";
		
		oss<<"}";
	}
	
	oss<<"]}";
	
	return oss.str();
}
