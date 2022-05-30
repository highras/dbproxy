#ifndef Config_Monitor_H
#define Config_Monitor_H

#include <list>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include "rijndael.h"
#include "TableManager.h"
#include "TableManagerBuilder.h"

class ConfigMonitor
{
	struct ConfigurationDatabaseInfo
	{
		std::vector<std::string> hosts;
		int port;
		int timeout;
		std::string database;
		std::string username;
		std::string password;
		int checkInterval;
		bool enableConfuse;
	};

	struct ConfuseDecryptor
	{
	private:
		size_t _pos;
		uint8_t _iv[16];
		rijndael_context _enCtx;

	public:
		ConfuseDecryptor(const std::string& user, const std::string& password);
		bool decrypt(std::string& user, std::string& password);
	};
	
	std::mutex _mutex;
	TableManagerPtr _tableManager;
	std::list<TableManagerPtr> _recycledTableManagers;
	
	bool _needRefresh;
	ConfigurationDatabaseInfo _cfgDBInfo;
	
	std::thread _monitor;
	std::atomic<bool> _willExit;

private:
	std::shared_ptr<MySQLClient> createMySQLClient(int& host_index);
	
	void recycleTableManagers();
	bool fetchDBConfiguration_DatabaseInfo(MySQLClient *, TableManagerBuilder &);
	bool fetchDBConfiguration_TableInfo(MySQLClient *, TableManagerBuilder &);
	bool fetchDBConfiguration_SplitTableInfo(MySQLClient *, TableManagerBuilder &);
	bool fetchDBConfiguration_SplitRangeInfo(MySQLClient *, TableManagerBuilder &);
	
	TableManagerPtr initTableManager(MySQLClient *, TableManagerPtr);
	int64_t getConfigurationUpdateTime(MySQLClient *);
	int64_t getSplitRangeSpan(MySQLClient *);
	int64_t getSecondaryRangeSplitNumberBase(MySQLClient *);
	void monitor_thread();

public:
	ConfigMonitor(const std::string& project = std::string());
	virtual ~ConfigMonitor();
	
	inline void refresh() { std::lock_guard<std::mutex> lck (_mutex); _needRefresh = true; }
	inline TableManagerPtr getTableManager() { std::lock_guard<std::mutex> lck (_mutex); return _tableManager; }
	
	std::string statusInJSON();
};


#endif
