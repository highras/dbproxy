#ifndef Task_Package_H
#define Task_Package_H

#include "MySQLClient.h"
#include "FPMessage.h"
#include "IQuestProcessor.h"

using namespace fpnn;

#define FPNN_DBPROXY_AGGREGATED_TASK_MUTEX_COUNT 64

//========================================//
//- Aggregated Task
//========================================//
class AggregatedTask
{
public:
	enum TaskType
	{
		AggregateIntIds,
		AggregateStringIds,
		AggregateAllTables,
	};

	struct UnitInfo
	{
		int equivalentTableHintId;
		std::set<int64_t> hintInts;
		std::set<std::string> hintStrings;
	};

	typedef std::shared_ptr<UnitInfo> UnitInfoPtr;

private:
	static std::atomic<uint32_t> _mutexIndex;
	static std::mutex _mutexPool[FPNN_DBPROXY_AGGREGATED_TASK_MUTEX_COUNT];

	std::mutex* _mutex;
	enum TaskType _type;
	IAsyncAnswerPtr _asyncAnswer;
	
	std::map<int, UnitInfoPtr> _unitInfoMap;
	std::map<int, QueryResultPtr> _resultMap;
	UnitInfoPtr _invalidUnitInfo;

	void finish();
	void fillFailedInfos(FPAWriter&);
	FPAnswerPtr buildAnswerForSelectQuery();

public:
	AggregatedTask(enum TaskType type, IAsyncAnswerPtr asyncAnswer, std::map<int, UnitInfoPtr>&& unitInfoMap, UnitInfoPtr invalidUnitInfo = nullptr):
		_type(type), _asyncAnswer(asyncAnswer), _unitInfoMap(std::move(unitInfoMap)), _invalidUnitInfo(invalidUnitInfo)
	{
		int idx = (int)_mutexIndex++;
		idx %= FPNN_DBPROXY_AGGREGATED_TASK_MUTEX_COUNT;
		_mutex = &(_mutexPool[idx]);
	}

	AggregatedTask(IAsyncAnswerPtr asyncAnswer, const std::set<int64_t>& equivalentTableIds):
		_type(AggregateAllTables), _asyncAnswer(asyncAnswer)
	{
		int idx = (int)_mutexIndex++;
		idx %= FPNN_DBPROXY_AGGREGATED_TASK_MUTEX_COUNT;
		_mutex = &(_mutexPool[idx]);

		for (int64_t hintId: equivalentTableIds)
			_unitInfoMap[(int)hintId] = nullptr;
	}

	~AggregatedTask()
	{
		finish();
	}

	void fillResult(int equivalentTableHintId, QueryResultPtr result)	//-- If failed, don't call this function.
	{
		std::lock_guard<std::mutex> lck (*_mutex);
		_resultMap[equivalentTableHintId] = result;
		_unitInfoMap.erase(equivalentTableHintId);
	}
};
typedef std::shared_ptr<AggregatedTask> AggregatedTaskPtr;

//========================================//
//- Task Package
//========================================//
class TaskPackage
{
protected:
	bool _processed;
	std::string _cluster;
	std::string _databaseName;
	IAsyncAnswerPtr _asyncAnswer;

	int _aggregatedTableHintId;
	AggregatedTaskPtr _aggregatedTask;

	static int _mySQLRepingInterval;
	
public:
	TaskPackage(const std::string& cluster, IAsyncAnswerPtr asyncAnswer): _processed(false), _cluster(cluster), _asyncAnswer(asyncAnswer) {}
	TaskPackage(int tableHintId, const std::string& cluster, AggregatedTaskPtr aggregatedTask): _processed(false),
		_cluster(cluster), _aggregatedTableHintId(tableHintId), _aggregatedTask(aggregatedTask) {}
	virtual ~TaskPackage();
	
	void setDatabaseName(const std::string& databaseName) { _databaseName = databaseName; }
	const std::string& cluster() { return _cluster; }
	
	//-- All finish functions are used under unaggregated mode.
	void finish(const char* errInfo);
	void finish(int code, const char* errInfo);
	void finish(FPAnswerPtr answer);

	virtual void processTask(MySQLClient *mySQL) throw () = 0;

	static void setMySQLRepingInterval(int interval);
	static bool setSuffix(const std::string& tableName, std::string& sql, const char* suffix); 
};
typedef std::shared_ptr<TaskPackage> TaskPackagePtr;

//========================================//
//- Query Task
//========================================//
class QueryTask: public TaskPackage
{
protected:
	std::string _sql;
	std::string _tableName;

public:
	QueryTask(const std::string& sql, const std::string& table_name, const std::string& cluster, IAsyncAnswerPtr asyncAnswer):
		TaskPackage(cluster, asyncAnswer), _sql(sql), _tableName(table_name) {}
	QueryTask(const std::string& sql, const std::string& table_name, const std::string& cluster, int tableHintId, AggregatedTaskPtr aggregatedTask):
		TaskPackage(tableHintId, cluster, aggregatedTask), _sql(sql), _tableName(table_name) {}
	virtual ~QueryTask() {}

	inline std::string& tableName() { return _tableName; }
	inline std::string& sql() { return _sql; }

	virtual void processTask(MySQLClient *mySQL) throw ();
};
typedef std::shared_ptr<QueryTask> QueryTaskPtr;

//========================================//
//- Params Query Task
//========================================//
class ParamsQueryTask: public QueryTask
{
	std::vector<std::string> _params;

	bool assemble(MySQLClient *);

public:
	ParamsQueryTask(const std::string& sql, const std::string& table_name, const std::string& cluster, std::vector<std::string>&& params, IAsyncAnswerPtr asyncAnswer):
		QueryTask(sql, table_name, cluster, asyncAnswer), _params(std::move(params)) {} //{ _params.swap(params); }
	ParamsQueryTask(const std::string& sql, const std::string& table_name, const std::string& cluster, const std::vector<std::string>& params, int tableHintId, AggregatedTaskPtr aggregatedTask):
		QueryTask(sql, table_name, cluster, tableHintId, aggregatedTask), _params(params) {}
	virtual ~ParamsQueryTask() {}

	virtual void processTask(MySQLClient *mySQL) throw ();

	static bool preassemble(const std::string& sql, const std::vector<std::string>& params, std::string& semisql, std::vector<std::string>& restParams);
};
typedef std::shared_ptr<ParamsQueryTask> ParamsQueryTaskPtr;

//========================================//
//- Transaction Task
//========================================//
class TransactionTask: public TaskPackage
{
public:
	std::vector<int64_t> _hintIds;
	std::vector<std::string> _tableNames;
	std::vector<std::string> _sqls;

public:
	TransactionTask(const std::string& cluster, IAsyncAnswerPtr asyncAnswer): TaskPackage(cluster, asyncAnswer) {}
	virtual ~TransactionTask() {}

	virtual void processTask(MySQLClient *mySQL) throw ();

	using TaskPackage::finish;
	void finish(int code, int sql_index, const char* reason);
};
typedef std::shared_ptr<TransactionTask> TransactionTaskPtr;

#endif
