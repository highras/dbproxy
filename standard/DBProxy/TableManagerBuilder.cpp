#include <algorithm>
#include "AutoRelease.h"
#include "FPLog.h"
#include "TableManagerBuilder.h"

int TableManagerBuilder::_perThreadPoolInitCount = 5;
int TableManagerBuilder::_perThreadPoolAppendCount = 2;
int TableManagerBuilder::_perThreadPoolPerfectCount = 20;
int TableManagerBuilder::_perThreadPoolMaxCount = 20;
int TableManagerBuilder::_perThreadPoolTempThreadLatencySeconds = 60;

void TableManagerBuilder::config(int perThreadPoolInitCount, int perThreadPoolAppendCount, int perThreadPoolPerfectCount, int perThreadPoolMaxCount, int perThreadPoolTempThreadLatencySeconds)
{
	_perThreadPoolInitCount = perThreadPoolInitCount;
	_perThreadPoolAppendCount = perThreadPoolAppendCount;
	_perThreadPoolPerfectCount = perThreadPoolPerfectCount;
	_perThreadPoolMaxCount = perThreadPoolMaxCount;
	_perThreadPoolTempThreadLatencySeconds = perThreadPoolTempThreadLatencySeconds;
}

//=============================================//
//-	TableManagerBuilder::ConstructureInfo
//=============================================//
TableManagerBuilder::ConstructureInfo::~ConstructureInfo()
{
	for (auto& tsiPair: _tableSplittingInfos)
		delete tsiPair.second;

	for (auto& rsiPair: _rangeSplittingInfos)
		for (RangeSplittingInfo* rsi: rsiPair.second)
			delete rsi;
}

DatabaseTaskQueuePtr TableManagerBuilder::ConstructureInfo::findDatabaseTaskQueue(int masterServerId)
{
	std::map<int, DatabaseTaskQueuePtr>::iterator it = _dbTaskQueues.find(masterServerId);
	if (it != _dbTaskQueues.end())
		return it->second;
	else
		return nullptr;
}

//=============================================//
//-	TableManagerBuilder
//=============================================//
TableManagerBuilder::TableManagerBuilder(int64_t range_span, int secondary_split_table_number_base, int64_t update_time):
	_splitSpan(range_span)
{
	_constructureInfo = new ConstructureInfo;
	_tableManager = std::make_shared<TableManager>(range_span, secondary_split_table_number_base, update_time);
}

TableManagerBuilder::~TableManagerBuilder()
{
	if (_constructureInfo)
		delete _constructureInfo;
	
	for (auto& tiPair: _tableInfos)
		delete tiPair.second;

	for (auto& rtqPair: _rangedTaskQueues)
	{
		for (RangeDatabaseNode* rdn: rtqPair.second)
			delete rdn;
	}
}

void TableManagerBuilder::addDatabaseInfo(DatabaseInfo * di)
{
	DatabaseInfoPtr dip(di);
	_constructureInfo->_dbCollection[di->serverId] = dip;
}

bool TableManagerBuilder::addTableInfo(TableInfo * ti)
{
	if (ti->splitByRange)
	{
		if (ti->splitSpan < 0)
			ti->splitSpan = _splitSpan;
		if (ti->secondarySplit && ti->seconddarySplitSpan <= 0)
		{
			LOG_ERROR("[Config Error] Table: '%s' for db categroy '%s', enable secondary split, but secondary split span is %d",
				ti->tableName.c_str(), ti->databaseCategory.c_str(), ti->seconddarySplitSpan);
			return false;
		}
	}
	else
	{
		if (ti->tableCount == 0)
			ti->tableCount = 1;
	}

	_tableInfos[ti->tableName] = ti;
	return true;
}
void TableManagerBuilder::addTableSplittingInfo(TableSplittingInfo *tsi)
{
	TableTaskHint tth(*tsi);
	_constructureInfo->_tableSplittingInfos.insert(std::make_pair(tth, tsi));
}

bool TableManagerBuilder::addRangeSplittingInfo(RangeSplittingInfo *rsi)
{
	if (rsi->indexType <0 || rsi->indexType > 2)
	{
		LOG_ERROR("[Config Error] db category '%s', split database index %d, index type is config error.",
			rsi->databaseCategory.c_str(), rsi->index);
		return false;
	}

	std::map<std::string, std::vector<RangeSplittingInfo*>>::iterator iter = _constructureInfo->_rangeSplittingInfos.find(rsi->databaseCategory);
	if (iter != _constructureInfo->_rangeSplittingInfos.end())
	{
		iter->second.push_back(rsi);
	}
	else
	{
		std::vector<RangeSplittingInfo*> tmpList;
		tmpList.push_back(rsi);
		_constructureInfo->_rangeSplittingInfos[rsi->databaseCategory] = tmpList;
	}
	return true;
}

bool TableManagerBuilder::init_status_check()
{
	if (_constructureInfo->_dbCollection.empty())
	{
		LOG_ERROR("[Config Error] server_info is empty or all invalid!");
		return false;
	}
	if (_tableInfos.empty())
	{
		LOG_ERROR("[Config Error] table_info is empty or all invalid!");
		return false;
	}
	if (_constructureInfo->_tableSplittingInfos.empty() && _constructureInfo->_rangeSplittingInfos.empty())
	{
		LOG_ERROR("[Config Error] All tables and categories infos are empty or invalid!");
		return false;
	}
	return true;
}

bool TableManagerBuilder::init_step5_dbCollection_to_dbTaskQueues(TableManagerPtr oldTableManager)
{
	for (auto& dbPair: _constructureInfo->_dbCollection)
	{
		if (dbPair.second->master_id != 0)
			continue;

		DatabaseTaskQueuePtr dtqp(new DatabaseTaskQueue);
		dtqp->masterDB = dbPair.second;
		dtqp->databaseList.push_back(dbPair.second);

		_constructureInfo->_dbTaskQueues[dbPair.second->serverId] = dtqp;
	}

	for (auto& dbPair: _constructureInfo->_dbCollection)
	{
		if (dbPair.second->master_id == 0)
			continue;

		std::map<int, DatabaseTaskQueuePtr>::iterator it = _constructureInfo->_dbTaskQueues.find(dbPair.second->master_id);
		if (it != _constructureInfo->_dbTaskQueues.end())
		{
			it->second->databaseList.push_back(dbPair.second);
		}
		else
		{
			LOG_ERROR("[Config Error] Slave Database id %d, at %s:%d, master db id %d is not found.",
					dbPair.second->serverId, dbPair.second->host.c_str(), dbPair.second->port, dbPair.second->master_id);
			return false;
		}
	}

	if (oldTableManager)
	{
		std::map<int, DatabaseTaskQueuePtr> newTaskQueues;

		for (auto taskPair: _constructureInfo->_dbTaskQueues)
		{
			DatabaseTaskQueuePtr equivalent;
			for (auto taskQueue: oldTableManager->_usedTaskQueues)
			{
				if (taskPair.second == taskQueue)
				{
					equivalent = taskQueue;
					break;
				}
			}

			if (equivalent)
				newTaskQueues[taskPair.first] = equivalent;
			else
				newTaskQueues[taskPair.first] = taskPair.second;
		}

		_constructureInfo->_dbTaskQueues.swap(newTaskQueues);
	}
	return true;
}

bool TableManagerBuilder::init_step6_tableSplittingInfos_dbTaskQueues_to_tableTaskQueues()
{
	for (auto& tsiPair: _constructureInfo->_tableSplittingInfos)
	{
		if (_tableTaskQueues.find(tsiPair.first) != _tableTaskQueues.end())
		{
			LOG_ERROR("[Config Error] Database for split table %s, table number %d is duplicated.", tsiPair.first.tableName.c_str(), tsiPair.first.tableNumber);
			return false;
		}
		
		TableSplittingInfo* tsi = tsiPair.second;
		DatabaseTaskQueuePtr dtq = _constructureInfo->findDatabaseTaskQueue(tsi->serverId);
		if (dtq)
		{
			_tableTaskQueues.insert(std::make_pair(tsiPair.first, dtq));
			_usedTaskQueues.insert(dtq);
		}
		else
		{
			LOG_ERROR("[Config Error] Split table: %s, number %d, the host master database's id %d is not found.",
				tsi->tableName.c_str(), tsi->tableNumber, tsi->serverId);
			return false;
		}
	}
	return true;
}

bool rangeSplittingInfoComp(const RangeSplittingInfo* a, const RangeSplittingInfo* b)
{
	if (a->index == b->index)
		return a->indexType < b->indexType;
	else
		return a->index < b->index;
}
bool TableManagerBuilder::init_step7_rangeSplittingInfos_dbTaskQueues_to_rangedTaskQueues()
{
	for (auto& rsiPair: _constructureInfo->_rangeSplittingInfos)
	{
		std::sort(rsiPair.second.begin(), rsiPair.second.end(), rangeSplittingInfoComp);
		int count = rsiPair.second.back()->index + 1;

		std::vector<RangeDatabaseNode*> vec;
		PointerContainerGuard<std::vector, RangeDatabaseNode> pcg(&vec, false);

		for (int i = 0; i < count; i++)
			vec.push_back(new RangeDatabaseNode);

		for (RangeSplittingInfo* rsi: rsiPair.second)
		{
			RangeDatabaseNode* rdn = vec[rsi->index];

			bool configError = false;
			if (rsi->indexType == 0)
			{
				if (rdn->oddEvened || rdn->arr[0])
					configError = true;
			}
			else if (rsi->indexType == 1)
			{
				if (rdn->arr[0])
					configError = true;
			}
			else if (rsi->indexType == 2)
			{
				if (rdn->arr[1])
					configError = true;
				else if (rdn->arr[0] && !rdn->oddEvened)
					configError = true;
			}

			if (configError)
			{
				LOG_ERROR("[Config Error] db category '%s', split database index %d is config error.", rsiPair.first.c_str(), rsi->index);
				return false;
			}

			DatabaseTaskQueuePtr dtq = _constructureInfo->findDatabaseTaskQueue(rsi->serverId);
			if (!dtq)
			{
				LOG_ERROR("[Config Error] db category '%s', %s split database index %d is not found.",
					rsiPair.first.c_str(), (rsi->indexType ? ((rsi->indexType == 1) ? "odd" : "even") : ""), rsi->index);
				return false;
			}

			RangedDatabaseTaskQueue* rdtq = new RangedDatabaseTaskQueue;
			rdtq->databaseTaskQueue = dtq;
			rdtq->databaseName = rsi->databaseName;

			if (rsi->indexType == 0)
			{
				rdn->oddEvened = false;
				rdn->arr[0] = rdtq;
			}
			else if (rsi->indexType == 1)
			{
				rdn->oddEvened = true;
				rdn->arr[0] = rdtq;
			}
			else if (rsi->indexType == 2)
			{
				rdn->oddEvened = true;
				rdn->arr[1] = rdtq;
			}
			_usedTaskQueues.insert(dtq);
		}

		//-- do check
		for (size_t i = 0; i < vec.size(); i++)
		{
			if (vec[i]->oddEvened)
			{
				if (vec[i]->arr[0] == NULL)
				{
					LOG_ERROR("[Config Error] db category '%s', odd split database index %d is unconfiged.", rsiPair.first.c_str(), i);
					return false;
				}
				if (vec[i]->arr[1] == NULL)
				{
					LOG_ERROR("[Config Error] db category '%s', even split database index %d is unconfiged.", rsiPair.first.c_str(), i);
					return false;
				} 
			}
			else
			{
				if (vec[i]->arr[0] == NULL)
				{
					LOG_ERROR("[Config Error] db category '%s', split database index %d is unconfiged.", rsiPair.first.c_str(), i);
					return false;
				}
			}
		}

		_rangedTaskQueues[rsiPair.first] = vec;
		pcg.release();
	}
	return true;
}

TableManagerPtr TableManagerBuilder::build(TableManagerPtr oldTableManager)
{
	/*
		5. _dbCollection => _dbTaskQueues
		6. _tableSplittingInfos + _dbTaskQueues => _tableTaskQueues
		7. _rangeSplittingInfos + _dbTaskQueues => _rangedTaskQueues
		8. _dbInfos => enable Thread Pool
		9. clean _constructureInfo
	*/

	if (!init_status_check())
		return nullptr;
	
	/* 5. _dbCollection => _dbTaskQueues */
	if (!init_step5_dbCollection_to_dbTaskQueues(oldTableManager))
		return nullptr;
	
	/* 6. _tableSplittingInfos + _dbTaskQueues => _tableTaskQueues */
	if (!init_step6_tableSplittingInfos_dbTaskQueues_to_tableTaskQueues())
		return nullptr;

	/* 7. _rangeSplittingInfos + _dbTaskQueues => _rangedTaskQueues */
	if (!init_step7_rangeSplittingInfos_dbTaskQueues_to_rangedTaskQueues())
		return nullptr;

	/* 8. _tableTaskQueues.databaseList => enable Thread Pool */
	for (auto& taskQueuePtr: _usedTaskQueues)
	{
		if (taskQueuePtr->inited)
			continue;

		for (auto& dbInfoPtr: taskQueuePtr->databaseList)
		{
			if (dbInfoPtr->master_id == 0)
				dbInfoPtr->enableThreadPool(&(taskQueuePtr->queue), _perThreadPoolInitCount, _perThreadPoolAppendCount,
					_perThreadPoolPerfectCount, _perThreadPoolMaxCount, _perThreadPoolTempThreadLatencySeconds);
			else
				dbInfoPtr->enableThreadPool(taskQueuePtr->queue.readQueue(), _perThreadPoolInitCount, _perThreadPoolAppendCount,
					_perThreadPoolPerfectCount, _perThreadPoolMaxCount, _perThreadPoolTempThreadLatencySeconds);
		}

		taskQueuePtr->inited = true;
	}
	
	/* 9. clean _constructureInfo */
	delete _constructureInfo;
	_constructureInfo = NULL;

	_tableManager->_tableInfos.swap(_tableInfos);
	_tableManager->_tableTaskQueues.swap(_tableTaskQueues);
	_tableManager->_rangedTaskQueues.swap(_rangedTaskQueues);
	_tableManager->_usedTaskQueues.swap(_usedTaskQueues);

	return _tableManager;
}
