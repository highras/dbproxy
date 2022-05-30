#include "DataRouterQuestProcessor.h"
#include "FPWriter.h"
#include "FPReader.h"
#include "jenkins.h"
#include "FpnnError.h"
#include "SQLParser.h"
#include "TaskPackage.h"
#include "DataRouterErrorInfo.h"

#define ONLY_HASH_TABLE(needCheck, quest, tableName)	{ if (needCheck) { \
		bool splitByRange; \
		if (tm->splitType(tableName, splitByRange)) { \
			if (splitByRange) \
				return ErrorInfo::disabledAnswer(quest, "String hint id cannot be applied with range split type."); \
		} else return ErrorInfo::tableNotFoundAnswer(quest); }}

std::string DataRouterQuestProcessor::infos()
{
	return _monitor.statusInJSON();
}

FPAnswerPtr DataRouterQuestProcessor::normalQuery(const FPQuestPtr quest, int64_t hintId, std::string& tableName, const std::string& sql, bool master, bool onlyHashTable)
{
	bool forceMasterTask;
	if (!SQLParser::pretreatSQL(sql, forceMasterTask, (tableName.empty() ? &tableName : NULL)))
		return ErrorInfo::disabledAnswer(quest);
	
	std::shared_ptr<TableManager> tm = _monitor.getTableManager();
	if (!tm)
		return ErrorInfo::unconfiguredAnswer(quest);

	ONLY_HASH_TABLE(onlyHashTable, quest, tableName)

	std::shared_ptr<IAsyncAnswer> async = genAsyncAnswer(quest);
	QueryTaskPtr task = std::make_shared<QueryTask>(sql, tableName, async);

	tm->query(hintId, master | forceMasterTask, task);
	return nullptr;
}
FPAnswerPtr DataRouterQuestProcessor::paramsQuery(const FPQuestPtr quest, int64_t hintId, std::string& tableName, const std::string& sql, const std::vector<std::string>& params, bool master, bool onlyHashTable)
{
	std::string semisql;
	std::vector<std::string> restParams;

	bool forceMasterTask;
	if (!ParamsQueryTask::preassemble(sql, params, semisql, restParams))
		return ErrorInfo::invalidParametersAnswer(quest);
	if (!SQLParser::pretreatSQL(semisql, forceMasterTask, (tableName.empty() ? &tableName : NULL)))
		return ErrorInfo::disabledAnswer(quest);
	
	std::shared_ptr<TableManager> tm = _monitor.getTableManager();
	if (!tm)
		return ErrorInfo::unconfiguredAnswer(quest);

	ONLY_HASH_TABLE(onlyHashTable, quest, tableName)

	std::shared_ptr<IAsyncAnswer> async = genAsyncAnswer(quest);
	ParamsQueryTaskPtr task = std::make_shared<ParamsQueryTask>(semisql, tableName, std::move(restParams), async);

	tm->query(hintId, master | forceMasterTask, task);
	return nullptr;
}
FPAnswerPtr DataRouterQuestProcessor::query(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	int64_t hintId = args->wantInt("hintId");
	if (hintId < 0)
		return ErrorInfo::negativeHintIdAnswer(quest);

	std::string tableName = args->get("tableName", std::string());
	std::string sql = args->want("sql", std::string());
	bool master = args->getBool("master", false);

	std::vector<std::string> params;
	params = args->get("params", params);

	SQLParser::extractSQL(sql);

	if (params.size())
		return paramsQuery(quest, hintId, tableName, sql, params, master);
	else
		return normalQuery(quest, hintId, tableName, sql, master);
}
AggregatedTaskPtr DataRouterQuestProcessor::generateAggregatedTask(std::shared_ptr<TableManager> tm, const FPQuestPtr quest, const std::string& tableName, const std::vector<int64_t>& hintIds, std::set<int64_t>& equivalentTableIds)
{
	std::map<int64_t, std::set<int64_t>> hintMap;
	std::set<int64_t> invalidHintIds;
	if (!tm->reformHintIds(tableName, hintIds, hintMap, invalidHintIds))
		return nullptr;

	std::map<int, AggregatedTask::UnitInfoPtr> shardingMap;
	for (auto hintPair: hintMap)
	{
		AggregatedTask::UnitInfoPtr unitInfo(new AggregatedTask::UnitInfo);
		unitInfo->equivalentTableHintId = hintPair.first;
		unitInfo->hintInts = hintPair.second;
		shardingMap[hintPair.first] = unitInfo;
		equivalentTableIds.insert(hintPair.first);
	}

	std::shared_ptr<IAsyncAnswer> async = genAsyncAnswer(quest);
	AggregatedTaskPtr aggTask = nullptr;

	if (!invalidHintIds.size())
		aggTask = std::make_shared<AggregatedTask>(AggregatedTask::AggregateIntIds, async, std::move(shardingMap));
	else
	{
		AggregatedTask::UnitInfoPtr invalidInfo(new AggregatedTask::UnitInfo);
		invalidInfo->hintInts = invalidHintIds;

		aggTask = std::make_shared<AggregatedTask>(AggregatedTask::AggregateIntIds, async, std::move(shardingMap), invalidInfo);
	}
	return aggTask;
}
AggregatedTaskPtr DataRouterQuestProcessor::generateAggregatedTask(std::shared_ptr<TableManager> tm, const FPQuestPtr quest, const std::string& tableName, const std::vector<std::string>& hintStrings, std::set<int64_t>& equivalentTableIds)
{
	//-- hash string ids to int ids.
	std::map<int64_t, std::set<std::string>> hashMapping;
	std::vector<int64_t> hintIds;
	for (const std::string& value: hintStrings)
	{
		int64_t hash = (int64_t)jenkins_hash(value.c_str(), value.length(), 0);
		hintIds.push_back(hash);
		hashMapping[hash].insert(value);
	}

	//-- reform ids.
	std::map<int64_t, std::set<int64_t>> hintMap;
	std::set<int64_t> invalidHintIds;
	if (!tm->reformHintIds(tableName, hintIds, hintMap, invalidHintIds))
		return nullptr;

	std::map<int, AggregatedTask::UnitInfoPtr> shardingMap;
	for (auto hintPair: hintMap)
	{
		AggregatedTask::UnitInfoPtr unitInfo(new AggregatedTask::UnitInfo);
		unitInfo->equivalentTableHintId = hintPair.first;
		for (int64_t hash: hintPair.second)
		{
			unitInfo->hintStrings.insert(hashMapping[hash].begin(), hashMapping[hash].end());
		}
		shardingMap[hintPair.first] = unitInfo;
		equivalentTableIds.insert(hintPair.first);
	}

	std::shared_ptr<IAsyncAnswer> async = genAsyncAnswer(quest);
	AggregatedTaskPtr aggTask;

	if (!invalidHintIds.size())
		aggTask = std::make_shared<AggregatedTask>(AggregatedTask::AggregateStringIds, async, std::move(shardingMap));
	else
	{
		AggregatedTask::UnitInfoPtr invalidInfo(new AggregatedTask::UnitInfo);
		for (int64_t hash: invalidHintIds)
			invalidInfo->hintStrings.insert(hashMapping[hash].begin(), hashMapping[hash].end());

		aggTask = std::make_shared<AggregatedTask>(AggregatedTask::AggregateStringIds, async, std::move(shardingMap), invalidInfo);
	}
	return aggTask;
}
template<typename T>
FPAnswerPtr DataRouterQuestProcessor::sharedingQuery(const FPQuestPtr quest, const std::vector<T>& hintIds, std::string& tableName, const std::string& sql, bool master, bool onlyHashTable)
{
	bool forceMasterTask;
	if (!SQLParser::pretreatSelectSQL(sql, forceMasterTask, (tableName.empty() ? &tableName : NULL)))
		return ErrorInfo::disabledAnswer(quest);
	
	std::shared_ptr<TableManager> tm = _monitor.getTableManager();
	if (!tm)
		return ErrorInfo::unconfiguredAnswer(quest);

	ONLY_HASH_TABLE(onlyHashTable, quest, tableName)

	std::set<int64_t> equivalentTableIds;
	AggregatedTaskPtr aggTask = generateAggregatedTask(tm, quest, tableName, hintIds, equivalentTableIds);
	if (!aggTask)
		return ErrorInfo::tableNotFoundAnswer(quest);

	for (auto equivalentId: equivalentTableIds)
	{
		QueryTaskPtr task = std::make_shared<QueryTask>(sql, tableName, equivalentId, aggTask);
		tm->query(equivalentId, master | forceMasterTask, task);
	}
	
	return nullptr;
}
template<typename T>
FPAnswerPtr DataRouterQuestProcessor::sharedingParamsQuery(const FPQuestPtr quest, const std::vector<T>& hintIds, std::string& tableName, const std::string& sql, const std::vector<std::string>& params, bool master, bool onlyHashTable)
{
	std::string semisql;
	std::vector<std::string> restParams;

	bool forceMasterTask;
	if (!ParamsQueryTask::preassemble(sql, params, semisql, restParams))
		return ErrorInfo::invalidParametersAnswer(quest);
	if (!SQLParser::pretreatSelectSQL(semisql, forceMasterTask, (tableName.empty() ? &tableName : NULL)))
		return ErrorInfo::disabledAnswer(quest);

	std::shared_ptr<TableManager> tm = _monitor.getTableManager();
	if (!tm)
		return ErrorInfo::unconfiguredAnswer(quest);

	ONLY_HASH_TABLE(onlyHashTable, quest, tableName)

	std::set<int64_t> equivalentTableIds;
	AggregatedTaskPtr aggTask = generateAggregatedTask(tm, quest, tableName, hintIds, equivalentTableIds);
	if (!aggTask)
		return ErrorInfo::tableNotFoundAnswer(quest);

	for (auto equivalentId: equivalentTableIds)
	{
		ParamsQueryTaskPtr task = std::make_shared<ParamsQueryTask>(semisql, tableName, restParams, equivalentId, aggTask);
		tm->query(equivalentId, master | forceMasterTask, task);
	}
	
	return nullptr;
}
FPAnswerPtr DataRouterQuestProcessor::sharedingAllTablesQuery(const FPQuestPtr quest, std::string& tableName, const std::string& sql, bool master)
{
	bool forceMasterTask;
	if (!SQLParser::pretreatSelectSQL(sql, forceMasterTask, (tableName.empty() ? &tableName : NULL)))
		return ErrorInfo::disabledAnswer(quest);
	
	std::shared_ptr<TableManager> tm = _monitor.getTableManager();
	if (!tm)
		return ErrorInfo::unconfiguredAnswer(quest);

	std::set<int64_t> equivalentTableIds;
	if (!tm->getAllSplitTablesHintIds(tableName, equivalentTableIds))
		return ErrorInfo::tableNotFoundAnswer(quest);

	std::shared_ptr<IAsyncAnswer> async = genAsyncAnswer(quest);
	AggregatedTaskPtr aggTask(new AggregatedTask(async, equivalentTableIds));
	
	for (auto equivalentId: equivalentTableIds)
	{
		QueryTaskPtr task = std::make_shared<QueryTask>(sql, tableName, equivalentId, aggTask);
		tm->query(equivalentId, master | forceMasterTask, task);
	}
	
	return nullptr;
}
FPAnswerPtr DataRouterQuestProcessor::sharedingAllTablesParamsQuery(const FPQuestPtr quest, std::string& tableName, const std::string& sql, const std::vector<std::string>& params, bool master)
{
	std::string semisql;
	std::vector<std::string> restParams;

	bool forceMasterTask;
	if (!ParamsQueryTask::preassemble(sql, params, semisql, restParams))
		return ErrorInfo::invalidParametersAnswer(quest);
	if (!SQLParser::pretreatSelectSQL(semisql, forceMasterTask, (tableName.empty() ? &tableName : NULL)))
		return ErrorInfo::disabledAnswer(quest);

	std::shared_ptr<TableManager> tm = _monitor.getTableManager();
	if (!tm)
		return ErrorInfo::unconfiguredAnswer(quest);

	std::set<int64_t> equivalentTableIds;
	if (!tm->getAllSplitTablesHintIds(tableName, equivalentTableIds))
		return ErrorInfo::tableNotFoundAnswer(quest);

	std::shared_ptr<IAsyncAnswer> async = genAsyncAnswer(quest);
	AggregatedTaskPtr aggTask(new AggregatedTask(async, equivalentTableIds));
	
	for (auto equivalentId: equivalentTableIds)
	{
		ParamsQueryTaskPtr task = std::make_shared<ParamsQueryTask>(semisql, tableName, restParams, equivalentId, aggTask);
		tm->query(equivalentId, master | forceMasterTask, task);
	}
	
	return nullptr;
}
FPAnswerPtr DataRouterQuestProcessor::iQuery(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::vector<int64_t> hintIds = args->want("hintIds", std::vector<int64_t>());
	std::string tableName = args->get("tableName", std::string());
	std::string sql = args->want("sql", std::string());
	bool master = args->getBool("master", false);

	std::vector<std::string> params;
	params = args->get("params", params);

	for (int64_t hintId: hintIds)
	{
		if (hintId < 0)
			return ErrorInfo::negativeHintIdAnswer(quest);
	}

	SQLParser::extractSQL(sql);

	if (hintIds.size() == 1)
	{
		if (params.size())
			return paramsQuery(quest, hintIds[0], tableName, sql, params, master);
		else
			return normalQuery(quest, hintIds[0], tableName, sql, master);
	}
	if (hintIds.size())
	{
		if (params.size())
			return sharedingParamsQuery(quest, hintIds, tableName, sql, params, master);
		else
			return sharedingQuery(quest, hintIds, tableName, sql, master);
	}
	else
	{
		if (params.size())
			return sharedingAllTablesParamsQuery(quest, tableName, sql, params, master);
		else
			return sharedingAllTablesQuery(quest, tableName, sql, master);
	}

	return nullptr;
}
FPAnswerPtr DataRouterQuestProcessor::sQuery(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::vector<std::string> hintIds = args->want("hintIds", std::vector<std::string>());
	std::string tableName = args->get("tableName", std::string());
	std::string sql = args->want("sql", std::string());
	bool master = args->getBool("master", false);

	std::vector<std::string> params;
	params = args->get("params", params);

	SQLParser::extractSQL(sql);

	if (hintIds.size() == 1)
	{
		int64_t hash = (int64_t)jenkins_hash(hintIds[0].c_str(), hintIds[0].length(), 0);

		if (params.size())
			return paramsQuery(quest, hash, tableName, sql, params, master, true);
		else
			return normalQuery(quest, hash, tableName, sql, master, true);
	}
	if (hintIds.size())
	{
		if (params.size())
			return sharedingParamsQuery(quest, hintIds, tableName, sql, params, master, true);
		else
			return sharedingQuery(quest, hintIds, tableName, sql, master, true);
	}
	else
	{
		if (params.size())
			return sharedingAllTablesParamsQuery(quest, tableName, sql, params, master);
		else
			return sharedingAllTablesQuery(quest, tableName, sql, master);
	}

	return nullptr;
}
FPAnswerPtr DataRouterQuestProcessor::splitInfo(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::string tableName = args->want("tableName", std::string());
	std::shared_ptr<TableManager> tm = _monitor.getTableManager();
	if (!tm)
		return ErrorInfo::unconfiguredAnswer(quest);

	SplitInfo info;
	if (tm->splitInfo(tableName, info))
	{
		if (info.splitByRange)
		{
			FPAWriter aw(7, quest);
			aw.param("splitByRange", true);
			aw.param("span", info.splitSpan);
			aw.param("count", info.rangeCount);

			aw.param("databaseCategory", info.databaseCategory);
			aw.param("splitHint", info.splitHint);
			aw.param("secondarySplit", info.secondarySplit);
			aw.param("secondarySplitSpan", info.seconddarySplitSpan);

			return aw.take();
		}
		else
			return FPAWriter(3, quest)("splitByRange", false)("tableCount", info.tableCount)("splitHint", info.splitHint);
	}
	else
		return ErrorInfo::tableNotFoundAnswer(quest);
}
FPAnswerPtr DataRouterQuestProcessor::categoryInfo(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::string category = args->want("databaseCategory", std::string());
	std::shared_ptr<TableManager> tm = _monitor.getTableManager();
	if (!tm)
		return ErrorInfo::unconfiguredAnswer(quest);

	DatabaseCategoryInfo info;
	if (tm->databaseCategoryInfo(category, info))
	{
		int oddEvenCount = (int)info.oddEvenIndexes.size();

		FPAWriter aw(oddEvenCount ? 3 : 2, quest);
		aw.param("splitCount", info.totalCount);
		aw.param("oddEvenCount", oddEvenCount);

		if (oddEvenCount)
			aw.param("oddEvenIndexes", info.oddEvenIndexes);

		return aw.take();
	}
	else
		return ErrorInfo::tableNotFoundAnswer(quest);
}
FPAnswerPtr DataRouterQuestProcessor::reformHintIds(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::vector<int64_t> hintIds = args->want("hintIds", std::vector<int64_t>());
	std::string tableName = args->want("tableName", std::string());

	if (hintIds.empty())
	{
		FPAWriter aw(2, quest);
		aw.paramArray("hintPairs", 0);
		aw.paramArray("invalidIds", 0);

		return aw.take();
	}

	std::shared_ptr<TableManager> tm = _monitor.getTableManager();
	if (!tm)
		return ErrorInfo::unconfiguredAnswer(quest);

	std::set<int64_t> invalidHintIds;
	std::map<int64_t, std::set<int64_t>> hintMap;

	if (tm->reformHintIds(tableName, hintIds, hintMap, invalidHintIds))
	{
		FPAWriter aw(2, quest);

		aw.paramArray("hintPairs", hintMap.size());
		for (auto& hintMapPair: hintMap)
		{
			aw.paramArray(2);
			aw.param(hintMapPair.first);
			aw.param(hintMapPair.second);
		}

		aw.param("invalidIds", invalidHintIds);

		return aw.take();
	}
	else
		return ErrorInfo::tableNotFoundAnswer(quest);

}
FPAnswerPtr DataRouterQuestProcessor::getAllSplitTablesHintIds(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::string tableName = args->want("tableName", std::string());
	std::shared_ptr<TableManager> tm = _monitor.getTableManager();
	if (!tm)
		return ErrorInfo::unconfiguredAnswer(quest);

	std::set<int64_t> hintIds;
	if (tm->getAllSplitTablesHintIds(tableName, hintIds))
	{
		FPAWriter aw(1, quest);
		aw.paramArray("hintIds", hintIds.size());

		for (int64_t id: hintIds)
			aw.param(id);

		return aw.take();
	}
	else
		return ErrorInfo::tableNotFoundAnswer(quest);
}
FPAnswerPtr DataRouterQuestProcessor::refresh(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	_monitor.refresh();
	return FPAWriter::emptyAnswer(quest);
}
void DataRouterQuestProcessor::uniformTransactionQuery(const FPQuestPtr quest, TransactionTaskPtr task)
{
	if (task->_sqls.empty() || task->_hintIds.size() != task->_tableNames.size() || task->_hintIds.size() != task->_sqls.size())
	{
		task->finish(ErrorInfo::disabledCode, "Invalid transaction. Parameters cannot matched.");
		return;
	}

	bool tmp;
	for (size_t i = 0; i < task->_sqls.size(); i++)
	{
		SQLParser::extractSQL(task->_sqls[i]);
		if (!SQLParser::pretreatSQL(task->_sqls[i], tmp, NULL))
		{
			task->finish(ErrorInfo::disabledCode, i, "Invalid statement.");
			return;
		}
	}

	std::shared_ptr<TableManager> tm = _monitor.getTableManager();
	if (tm)
		tm->transaction(task);
	else
		task->finish(ErrorInfo::unconfiguredAnswer(quest));
}
FPAnswerPtr DataRouterQuestProcessor::transaction(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::shared_ptr<IAsyncAnswer> async = genAsyncAnswer(quest);
	TransactionTaskPtr task = std::make_shared<TransactionTask>(async);

	task->_hintIds = args->want("hintIds", std::vector<int64_t>());
	task->_tableNames = args->want("tableNames", std::vector<std::string>());
	task->_sqls = args->want("sqls", std::vector<std::string>());

	for (int64_t hintId: task->_hintIds)
	{
		if (hintId < 0)
			return ErrorInfo::negativeHintIdAnswer(quest);
	}

	uniformTransactionQuery(quest, task);
	return nullptr;
}
FPAnswerPtr DataRouterQuestProcessor::sTransaction(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::shared_ptr<IAsyncAnswer> async = genAsyncAnswer(quest);
	TransactionTaskPtr task = std::make_shared<TransactionTask>(async);

	std::vector<std::string> hintStrings = args->want("hintIds", std::vector<std::string>());
	if (hintStrings.size() > 0)
	{
		task->_hintIds.reserve(hintStrings.size());
		for (size_t i = 0; i < hintStrings.size(); i++)
		{
			int64_t hash = (int64_t)jenkins_hash(hintStrings[i].c_str(), hintStrings[i].length(), 0);
			task->_hintIds.push_back(hash);
		}

		task->_tableNames = args->want("tableNames", std::vector<std::string>());
		task->_sqls = args->want("sqls", std::vector<std::string>());

		//-- Check table split type.
		std::shared_ptr<TableManager> tm = _monitor.getTableManager();
		if (!tm)
			task->finish(ErrorInfo::unconfiguredAnswer(quest));

		for (const std::string& tableName: task->_tableNames)
		{
			bool splitByRange;
			if (tm->splitType(tableName, splitByRange))
			{
				if (splitByRange)
				{
					task->finish(ErrorInfo::disabledCode, "String hint id cannot be applied with range split type.");
					return nullptr;
				}
			}
			else
			{
				task->finish(ErrorInfo::notFoundCode, "Table not found.");
				return nullptr;
			}
		}
	}

	uniformTransactionQuery(quest, task);
	return nullptr;
}
