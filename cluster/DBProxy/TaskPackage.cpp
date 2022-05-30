#include "FPLog.h"
#include "FPWriter.h"
#include "SQLParser.h"
#include "TaskPackage.h"
#include "DataRouterErrorInfo.h"

using fpnn::FPAWriter;

//========================================//
//- Aggregated Task
//========================================//
std::atomic<uint32_t> AggregatedTask::_mutexIndex(0);
std::mutex AggregatedTask::_mutexPool[FPNN_DBPROXY_AGGREGATED_TASK_MUTEX_COUNT];

void AggregatedTask::fillFailedInfos(FPAWriter& aw)
{
	if (_type == AggregateIntIds)
	{
		int failedCount = 0;
		for (auto unitPair: _unitInfoMap)
			failedCount += (int)unitPair.second->hintInts.size();

		if (failedCount)
		{
			aw.paramArray("failedIds", failedCount);
			for (auto unitPair: _unitInfoMap)
				for (auto idValue: unitPair.second->hintInts)
					aw.param(idValue);
		}

		if (_invalidUnitInfo)
		{
			aw.paramArray("invalidIds", _invalidUnitInfo->hintInts.size());
			for (auto idValue: _invalidUnitInfo->hintInts)
				aw.param(idValue);
		}	
	}
	else if (_type == AggregateStringIds)
	{
		int failedCount = 0;
		for (auto unitPair: _unitInfoMap)
			failedCount += (int)unitPair.second->hintStrings.size();

		if (failedCount)
		{
			aw.paramArray("failedIds", failedCount);
			for (auto unitPair: _unitInfoMap)
				for (auto idValue: unitPair.second->hintStrings)
					aw.param(idValue);
		}

		if (_invalidUnitInfo)
		{
			LOG_FATAL("Fatal logic error! If hintId is string type, it just only can applied with the tables split by hash, and without any invalidIds.");
			aw.paramArray("invalidIds", _invalidUnitInfo->hintStrings.size());
			for (auto idValue: _invalidUnitInfo->hintStrings)
				aw.param(idValue);
		}
	}
	else
	{
		aw.paramArray("failedIds", _unitInfoMap.size());
		for (auto unitPair: _unitInfoMap)
			aw.param(unitPair.first);
	}
}
FPAnswerPtr AggregatedTask::buildAnswerForSelectQuery()
{
	int rowsCount = 0;
	for (auto resultPair: _resultMap)
		rowsCount += (int)resultPair.second->rows.size();

	int errorPart = 0;
	if (_invalidUnitInfo)
		errorPart += 1;
	if (_unitInfoMap.size())
		errorPart += 1;

	FPAWriter aw(2 + errorPart, _asyncAnswer->getQuest());
	aw.param("fields", _resultMap.begin()->second->fields);
	aw.paramArray("rows", rowsCount);

	for (auto resultPair: _resultMap)
		for (auto row: resultPair.second->rows)
			aw.param(row);

	if (errorPart)
		fillFailedInfos(aw);

	return aw.take();
}
void AggregatedTask::finish()
{
	enum QueryResult::ResultType resultType = QueryResult::ErrorType;
	if (_resultMap.size())
		resultType = _resultMap.begin()->second->type;

	FPAnswerPtr answer;
	if (resultType == QueryResult::SelectType)
	{
		answer = buildAnswerForSelectQuery();
	}
	else if (resultType == QueryResult::ModifyType)
	{
		int errorPart = 0;
		if (_invalidUnitInfo)
			errorPart += 1;
		if (_unitInfoMap.size())
			errorPart += 1;

		FPAWriter aw(1 + errorPart, _asyncAnswer->getQuest());
		aw.paramArray("results", _resultMap.size());
		
		for (auto resultPair: _resultMap)
		{
			aw.paramArray(3);
			aw.param(resultPair.first);
			aw.param(resultPair.second->affectedRows);
			aw.param(resultPair.second->insertId);
		}

		if (errorPart)
			fillFailedInfos(aw);

		answer = aw.take();
	}

	if (!answer)
		answer = FPAWriter::errorAnswer(_asyncAnswer->getQuest(), ErrorInfo::internalErrorCode, "Unknown Error. No aggregated task executed.", ErrorInfo::raiser_DataRouter);

	_asyncAnswer->sendAnswer(answer);
}
//=============================================//
//-	TaskPackage
//=============================================//
int TaskPackage::_mySQLRepingInterval = 300;

TaskPackage::~TaskPackage()
{
	finish("Please try again. DBMan is exiting or refreshing.");
}

void TaskPackage::finish(const char* errInfo)
{
	if (_processed || !_asyncAnswer)
		return;
		
	FPAnswerPtr answer = FPAWriter::errorAnswer(_asyncAnswer->getQuest(), ErrorInfo::internalErrorCode, errInfo, ErrorInfo::raiser_DataRouter);
	finish(answer);
}

void TaskPackage::finish(int code, const char* errInfo)
{
	if (_processed || !_asyncAnswer)
		return;
		
	FPAnswerPtr answer = FPAWriter::errorAnswer(_asyncAnswer->getQuest(), code, errInfo, ErrorInfo::raiser_DataRouter);
	finish(answer);
}

void TaskPackage::finish(FPAnswerPtr answer)
{
	if (_processed || !_asyncAnswer)
		return;

	_processed = _asyncAnswer->sendAnswer(answer);
}

void TaskPackage::setMySQLRepingInterval(int interval)
{
	_mySQLRepingInterval = interval;
}

bool TaskPackage::setSuffix(const std::string& tableName, std::string& sql, const char* suffix)
{
	return SQLParser::addTableSuffix(sql, tableName, suffix);
}

//=============================================//
//-	QueryTask
//=============================================//
void QueryTask::processTask(MySQLClient *mySQL) throw ()
{
	try
	{
		bool connected = mySQL->connected();
		if (connected)
		{
			if (time(NULL) - mySQL->lastOperatedTime() >= _mySQLRepingInterval)
				connected = mySQL->ping();
		}
		if (!connected)
		{
			mySQL->cleanup();
			if (!mySQL->connect())
			{
				finish("Database connection lost.");
				return;
			}
		}
		
		if (_asyncAnswer)
		{
			FPAnswerPtr answer = mySQL->query(_databaseName, _sql, _asyncAnswer->getQuest());
			finish(answer);
		}
		else
		{
			QueryResultPtr result(new QueryResult);

			if (mySQL->query(_databaseName, _sql, *result))
				_aggregatedTask->fillResult(_aggregatedTableHintId, result);
			else
				LOG_ERROR("Aggregated task: table id %d, database: %s, sql:[%s] failed.",
					_aggregatedTableHintId, _databaseName.c_str(), _sql.c_str());
		}
	}
	catch (const std::exception &e)
	{
		finish(e.what());
	}
}

//=============================================//
//-	ParamsQueryTask
//=============================================//
bool ParamsQueryTask::assemble(MySQLClient *mySQL)
{
	mySQL->escapeStrings(_params);
	std::string realSql;

	size_t index = 0;
	size_t begin = 0;
	size_t pos = 0;
	while (true)
	{
		if (begin >= _sql.length())
		{
			_sql.swap(realSql);
			return true;
		}

		pos = _sql.find_first_of('?', begin);
		if (pos == std::string::npos)
		{
			if (index != _params.size())
				return false;

			realSql += _sql.substr(begin, std::string::npos);
			_sql.swap(realSql);
			return true;
		}

		if (index == _params.size())
			return false;

		realSql += _sql.substr(begin, pos - begin);
		realSql.append(_params[index]);

		begin = pos + 1;
		index += 1;
	}
}

bool ParamsQueryTask::preassemble(const std::string& sql, const std::vector<std::string>& params, std::string& semisql, std::vector<std::string>& restParams)
{
	semisql.clear();
	restParams.clear();
	size_t index = 0;

	size_t begin = 0;
	size_t pos = 0;
	while (true)
	{
		if (begin >= sql.length())
		{
			if (index < params.size())
				return false;
			else
				return true;
		}

		pos = sql.find_first_of('?', begin);
		if (pos == std::string::npos)
		{
			if (index != params.size())
				return false;

			semisql += sql.substr(begin, std::string::npos);
			return true;
		}

		if (index == params.size())
			return false;

		semisql += sql.substr(begin, pos - begin);

		if (pos > 0 && (sql.at(pos - 1) == '\'') && (sql.at(pos+1) == '\''))
		{
			restParams.push_back(params[index]);
			semisql.append("?");
		}
		else
		{
			semisql.append(params[index]);
		}

		begin = pos + 1;
		index += 1;
	}
}

void ParamsQueryTask::processTask(MySQLClient *mySQL) throw ()
{
	try
	{
		bool connected = mySQL->connected();
		if (connected)
		{
			if (time(NULL) - mySQL->lastOperatedTime() >= _mySQLRepingInterval)
				connected = mySQL->ping();
		}
		if (!connected)
		{
			mySQL->cleanup();
			if (!mySQL->connect())
			{
				finish("Database connection lost.");
				return;
			}
		}
		
		if (_asyncAnswer)
		{
			FPAnswerPtr answer;

			if (assemble(mySQL))
				answer = mySQL->query(_databaseName, _sql, _asyncAnswer->getQuest());
			else
				answer = ErrorInfo::invalidParametersAnswer(_asyncAnswer->getQuest());

			finish(answer);
		}
		else
		{
			if (assemble(mySQL))
			{
				QueryResultPtr result(new QueryResult);

				if (mySQL->query(_databaseName, _sql, *result))
					_aggregatedTask->fillResult(_aggregatedTableHintId, result);
				else
					LOG_ERROR("Aggregated task: table id %d, database: %s, sql:[%s] failed.",
						_aggregatedTableHintId, _databaseName.c_str(), _sql.c_str());
			}
			else
				LOG_ERROR("Assemble sql for aggregated task failed. Table id %d, database: %s, semisql:[%s] failed.",
						_aggregatedTableHintId, _databaseName.c_str(), _sql.c_str());
		}
	}
	catch (const std::exception &e)
	{
		finish(e.what());
	}
}

//=============================================//
//-	TransactionTask
//=============================================//
void TransactionTask::finish(int code, int sql_index, const char* reason)
{
	std::string ex;

	ex.append("Excepted index: ").append(std::to_string(sql_index));
	ex.append(" Excepted SQL: ").append(_sqls[sql_index]);
	ex.append(". Reason: ").append(reason);

	FPAnswerPtr answer = FPAWriter::errorAnswer(_asyncAnswer->getQuest(), code, ex, ErrorInfo::raiser_DataRouter);
	finish(answer);
}

void TransactionTask::processTask(MySQLClient *mySQL) throw ()
{
	try
	{
		bool connected = mySQL->connected();
		if (connected)
		{
			if (time(NULL) - mySQL->lastOperatedTime() >= _mySQLRepingInterval)
				connected = mySQL->ping();
		}
		if (!connected)
		{
			mySQL->cleanup();
			if (!mySQL->connect())
			{
				finish("Database connection lost.");
				return;
			}
		}
		
		FPAnswerPtr answer = mySQL->transaction(_databaseName, _sqls, _asyncAnswer->getQuest());
		finish(answer);
	}
	catch (const std::exception &e)
	{
		finish(e.what());
	}
}
