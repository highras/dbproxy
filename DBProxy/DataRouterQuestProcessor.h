#ifndef Data_Router_Quest_Processor_H
#define Data_Router_Quest_Processor_H

#include "IQuestProcessor.h"
#include "ConfigMonitor.h"
#include "SQLParser.h"

#ifndef DBProxy_Manager_Version
#include "FPZKClient.h"
#include "Setting.h"
#endif

using namespace fpnn;

class DataRouterQuestProcessor: public IQuestProcessor
{
	QuestProcessorClassPrivateFields(DataRouterQuestProcessor)

	ConfigMonitor _monitor;
	
#ifndef DBProxy_Manager_Version
	FPZKClientPtr _fpzk;
#endif

	FPAnswerPtr normalQuery(const FPQuestPtr quest, int64_t hintId, std::string& tableName, const std::string& cluster, const std::string& sql, bool master, bool onlyHashTable = false);
	FPAnswerPtr paramsQuery(const FPQuestPtr quest, int64_t hintId, std::string& tableName, const std::string& cluster, const std::string& sql, const std::vector<std::string>& params, bool master, bool onlyHashTable = false);
	
	AggregatedTaskPtr generateAggregatedTask(std::shared_ptr<TableManager> tm, const FPQuestPtr quest, const std::string& tableName, const std::string& cluster, const std::vector<int64_t>& hintIds, std::set<int64_t>& equivalentTableIds);
	AggregatedTaskPtr generateAggregatedTask(std::shared_ptr<TableManager> tm, const FPQuestPtr quest, const std::string& tableName, const std::string& cluster, const std::vector<std::string>& hintStrings, std::set<int64_t>& equivalentTableIds);
	
	template<typename T>
	FPAnswerPtr sharedingQuery(const FPQuestPtr quest, const std::vector<T>& hintIds, std::string& tableName, const std::string& cluster, const std::string& sql, bool master, bool onlyHashTable = false);
	template<typename T>
	FPAnswerPtr sharedingParamsQuery(const FPQuestPtr quest, const std::vector<T>& hintIds, std::string& tableName, const std::string& cluster, const std::string& sql, const std::vector<std::string>& params, bool master, bool onlyHashTable = false);
	
	FPAnswerPtr sharedingAllTablesQuery(const FPQuestPtr quest, std::string& tableName, const std::string& cluster, const std::string& sql, bool master);
	FPAnswerPtr sharedingAllTablesParamsQuery(const FPQuestPtr quest, std::string& tableName, const std::string& cluster, const std::string& sql, const std::vector<std::string>& params, bool master);
	void uniformTransactionQuery(const FPQuestPtr quest, TransactionTaskPtr task);

public:
	FPAnswerPtr query(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr iQuery(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr sQuery(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr splitInfo(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr categoryInfo(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr reformHintIds(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr getAllSplitTablesHintIds(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr refresh(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr transaction(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr sTransaction(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);

	virtual std::string infos();

	DataRouterQuestProcessor()
	{
		registerMethod("query", &DataRouterQuestProcessor::query);
		registerMethod("iQuery", &DataRouterQuestProcessor::iQuery);
		registerMethod("sQuery", &DataRouterQuestProcessor::sQuery);
		registerMethod("splitInfo", &DataRouterQuestProcessor::splitInfo);
		registerMethod("categoryInfo", &DataRouterQuestProcessor::categoryInfo);
		registerMethod("reformHintIds", &DataRouterQuestProcessor::reformHintIds);
		registerMethod("allSplitHintIds", &DataRouterQuestProcessor::getAllSplitTablesHintIds);
		registerMethod("refresh", &DataRouterQuestProcessor::refresh);
		registerMethod("transaction", &DataRouterQuestProcessor::transaction);
		registerMethod("sTransaction", &DataRouterQuestProcessor::sTransaction);

		SQLParser::init();
		
#ifndef DBProxy_Manager_Version
		if (Setting::setted("FPZK.client.fpzkserver_list"))
		{
			_fpzk = FPZKClient::create();
			_fpzk->registerService();
		}
#endif
	}

	QuestProcessorClassBasicPublicFuncs
};

#endif
