#include "hex.h"
#include "sha256.h"
#include "Setting.h"
#include "FPLog.h"
#include "FpnnError.h"
#include "ServerInfo.h"
#include "StringUtil.h"
#include "AutoRelease.h"
#include "ConfigMonitor.h"
#include "TaskPackage.h"

FPNN_(FpnnLogicError, InvalidConfigError)

ConfigMonitor::ConfuseDecryptor::ConfuseDecryptor(const std::string& user, const std::string& password): _pos(0)
{
	std::string conf(user);
	conf.append(password);

	unsigned char digest[32];
	sha256_checksum(digest, conf.data(), conf.length());

	memcpy(_iv, digest, 16);
	rijndael_setup_encrypt(&_enCtx, (const uint8_t *)digest + 16, 16);
}

bool ConfigMonitor::ConfuseDecryptor::decrypt(std::string& user, std::string& password)
{
	char* ubuf = (char*)malloc(user.length());
	char* pbuf = (char*)malloc(password.length());

	int ulen = unhexlify(ubuf, user.data(), (int)user.length());
	int plen = unhexlify(pbuf, password.data(), (int)password.length());

	AutoFreeGuard uafg(ubuf), pafg(pbuf);

	if (ulen < 0 || plen < 0)
	{
		LOG_ERROR("unhexlify confused config data failed.");
		return false;
	}

	char* deubuf = (char*)malloc(ulen);
	char* depbuf = (char*)malloc(plen);

	AutoFreeGuard deuafg(deubuf), depafg(depbuf);

	rijndael_cfb_encrypt(&_enCtx, false, (uint8_t *)ubuf, (uint8_t *)deubuf, ulen, _iv, &_pos);
	rijndael_cfb_encrypt(&_enCtx, false, (uint8_t *)pbuf, (uint8_t *)depbuf, plen, _iv, &_pos);

	user.assign(deubuf, ulen);
	password.assign(depbuf, plen);

	return true;
}

ConfigMonitor::ConfigMonitor(const std::string& project): _needRefresh(false), _willExit(false)
{
	std::string hosts = Setting::getString("DBProxy.ConfigureDB.host");

	StringUtil::split(hosts, " ,", _cfgDBInfo.hosts);
	_cfgDBInfo.port = Setting::getInt("DBProxy.ConfigureDB.port", 0);
	_cfgDBInfo.timeout = Setting::getInt("DBProxy.ConfigureDB.timeout", 0);
	_cfgDBInfo.database = Setting::getString("DBProxy.ConfigureDB.databaseName");
	_cfgDBInfo.username = Setting::getString("DBProxy.ConfigureDB.username");
	_cfgDBInfo.password = Setting::getString("DBProxy.ConfigureDB.password");
	_cfgDBInfo.checkInterval = Setting::getInt("DBProxy.ConfigureDB.checkInterval", 900);
	_cfgDBInfo.enableConfuse = Setting::getBool("DBProxy.ConfigureDB.enableConfuse", false);
	
	int perThreadPoolInitCount = Setting::getInt("DBProxy.perThreadPool.InitThreadCount", 10);
	int perThreadPoolAppendCount = Setting::getInt("DBProxy.perThreadPool.AppendThreadCount", 5);
	int perThreadPoolPerfectCount = Setting::getInt("DBProxy.perThreadPool.PerfectThreadCount", 100);
	int perThreadPoolMaxCount = Setting::getInt("DBProxy.perThreadPool.MaxThreadCount", 600);
	int mySQLPingInterval = Setting::getInt("DBProxy.mySQLPingInterval", 900);

	int perThreadPoolReadQueueMaxLength = Setting::getInt("DBProxy.perThreadPool.readQueue.MaxLength", 200000);
	int perThreadPoolWriteQueueMaxLength = Setting::getInt("DBProxy.perThreadPool.writeQueue.MaxLength", 200000);
	int perThreadPoolTempThreadLatencySeconds = Setting::getInt("DBProxy.perThreadPool.temporaryThread.latencySeconds", 60);

	MySQLClient::setDefaultConnectionCharacterSetName(Setting::getString("DBProxy.connection.characterSet.name", "utf8"));

	LOG_INFO("INFO: Load %d hosts", _cfgDBInfo.hosts.size());
	if (_cfgDBInfo.hosts.size() == 0)
		exit(1);

	TaskPackage::setMySQLRepingInterval(mySQLPingInterval);
	TableManager::config(perThreadPoolReadQueueMaxLength, perThreadPoolWriteQueueMaxLength);
	TableManagerBuilder::config(perThreadPoolInitCount, perThreadPoolAppendCount, perThreadPoolPerfectCount, perThreadPoolMaxCount, perThreadPoolTempThreadLatencySeconds);
		
	MySQLClient::MySQLClientInit();
	
	_monitor = std::thread(&ConfigMonitor::monitor_thread, this);
}

ConfigMonitor::~ConfigMonitor()
{
	_willExit = true;
	_monitor.join();

	_recycledTableManagers.clear();
	_tableManager.reset();

	MySQLClient::MySQLClientEnd();
}

void ConfigMonitor::recycleTableManagers()
{
	if (_recycledTableManagers.empty())
		return;
		
	std::list<TableManagerPtr> tmpList;
	while (!_recycledTableManagers.empty())
	{
		TableManagerPtr tmp = _recycledTableManagers.front();
		_recycledTableManagers.pop_front();
		
		if (!tmp->deletable())
			tmpList.push_back(tmp);
	}
	_recycledTableManagers.swap(tmpList);
}

bool ConfigMonitor::fetchDBConfiguration_DatabaseInfo(MySQLClient *mySQL, TableManagerBuilder& builder)
{
	ConfuseDecryptor* decrypt = NULL;
	if (_cfgDBInfo.enableConfuse)
		decrypt = new ConfuseDecryptor(_cfgDBInfo.username, _cfgDBInfo.password);

	AutoDeleteGuard<ConfuseDecryptor> adg(decrypt);

	const size_t limit = 10000;
	int lastDBid = 0;
	while (true)
	{
		std::ostringstream oss;
		oss<<"select server_id, master_sid, host, port, user, passwd, timeout, default_database_name from server_info";
		oss<<" where server_id > "<<lastDBid<<" order by server_id asc limit "<<limit;
		
		std::string sql = oss.str();
		QueryResult queryResult;
		std::vector<std::vector<std::string>> &result = queryResult.rows;
		if (!mySQL->query(_cfgDBInfo.database, sql, queryResult))
			return false;

		for (size_t i = 0; i < result.size(); i++)
		{
			DatabaseInfo *di = new DatabaseInfo;
			
			try
			{
				di->serverId = atoi(result[i][0].c_str());
				di->master_id = atoi(result[i][1].c_str());
				di->host = result[i][2];
				di->port = atoi(result[i][3].c_str());
				di->username = result[i][4];
				di->password = result[i][5];
				di->timeout = atoi(result[i][6].c_str());
				di->databaseName = result[i][7];

				if (decrypt)
					if (!decrypt->decrypt(di->username, di->password))
						return false;
				
				builder.addDatabaseInfo(di);
				lastDBid = di->serverId;
			}
			catch (...)
			{
				delete di;
				throw;
			}
		}
		
		if (result.size() < limit)
			return true;
	}
}

bool ConfigMonitor::fetchDBConfiguration_TableInfo(MySQLClient *mySQL, TableManagerBuilder& builder)
{
	const size_t limit = 10000;
	int lastTableId = 0;
	while (true)
	{
		std::ostringstream oss;
		oss<<"select id, table_name, split_type, range_span, database_category, secondary_split, secondary_split_span, table_count, hint_field";
		oss<<" from table_info where id > "<<lastTableId<<" order by id asc limit "<<limit;
		
		std::string sql = oss.str();
		QueryResult queryResult;
		std::vector<std::vector<std::string>> &result = queryResult.rows;
		if (!mySQL->query(_cfgDBInfo.database, sql, queryResult))
			return false;
			
		for (size_t i = 0; i < result.size(); i++)
		{
			TableInfo *ti = new TableInfo;
			
			try
			{
				lastTableId = atoi(result[i][0].c_str());
				
				ti->tableName = result[i][1];
				ti->splitByRange = (bool)atoi(result[i][2].c_str());

				ti->splitSpan = atoll(result[i][3].c_str());
				ti->databaseCategory = result[i][4];
				ti->secondarySplit = (bool)atoi(result[i][5].c_str());
				ti->seconddarySplitSpan = atoll(result[i][6].c_str());

				ti->tableCount = atoi(result[i][7].c_str());
				ti->splitHint = result[i][8];
					
				if (!builder.addTableInfo(ti))
				{
					delete ti;
					return false;
				}
			}
			catch (...)
			{
				delete ti;
				throw;
			}
		}
		
		if (result.size() < limit)
			return true;
	}
}

bool ConfigMonitor::fetchDBConfiguration_SplitTableInfo(MySQLClient *mySQL, TableManagerBuilder& builder)
{
	const size_t limit = 10000;
	int lastSplitTableId = 0;
	while (true)
	{
		std::ostringstream oss;
		oss<<"select id, table_name, table_number, server_id, database_name from split_table_info where id > ";
		oss<<lastSplitTableId<<" order by id asc limit "<<limit;
		
		std::string sql = oss.str();
		QueryResult queryResult;
		std::vector<std::vector<std::string>> &result = queryResult.rows;
		if (!mySQL->query(_cfgDBInfo.database, sql, queryResult))
			return false;
			
		for (size_t i = 0; i < result.size(); i++)
		{
			TableSplittingInfo *tsi = new TableSplittingInfo;
			
			try
			{
				lastSplitTableId = atoi(result[i][0].c_str());
				
				tsi->tableName = result[i][1];
				tsi->tableNumber = atoi(result[i][2].c_str());
				tsi->serverId = atoi(result[i][3].c_str());
				tsi->databaseName = result[i][4];
				
				builder.addTableSplittingInfo(tsi);
			}
			catch (...)
			{
				delete tsi;
				throw;
			}
		}
		
		if (result.size() < limit)
			return true;
	}
}

bool ConfigMonitor::fetchDBConfiguration_SplitRangeInfo(MySQLClient *mySQL, TableManagerBuilder& builder)
{
	const size_t limit = 10000;
	int lastSplitRangeId = 0;
	while (true)
	{
		std::ostringstream oss;
		oss<<"select id, database_category, split_index, index_type, database_name, server_id from split_range_info where id > ";
		oss<<lastSplitRangeId<<" order by id asc limit "<<limit;
		
		std::string sql = oss.str();
		QueryResult queryResult;
		std::vector<std::vector<std::string>> &result = queryResult.rows;
		if (!mySQL->query(_cfgDBInfo.database, sql, queryResult))
			return false;
			
		for (size_t i = 0; i < result.size(); i++)
		{
			RangeSplittingInfo *rsi = new RangeSplittingInfo;
			
			try
			{
				lastSplitRangeId = atoi(result[i][0].c_str());

				rsi->index = atoi(result[i][2].c_str());
				rsi->indexType = atoi(result[i][3].c_str());
				rsi->serverId = atoi(result[i][5].c_str());
				rsi->databaseCategory = result[i][1];
				rsi->databaseName = result[i][4];
				
				if (!builder.addRangeSplittingInfo(rsi))
				{
					delete rsi;
					return false;
				}
			}
			catch (...)
			{
				delete rsi;
				throw;
			}
		}
		
		if (result.size() < limit)
			return true;
	}
}
	
TableManagerPtr ConfigMonitor::initTableManager(MySQLClient *mySQL, TableManagerPtr currentTableManager)
{
	int64_t utime = getConfigurationUpdateTime(mySQL);
	if (utime <= 0)
	{
		LOG_FATAL("[Config Error] DBProxy config data update time is invalid.");
		return nullptr;
	}

	int64_t splitSpan = getSplitRangeSpan(mySQL);
	if (splitSpan <= 0)
	{
		LOG_FATAL("[Config ERROR] DBProxy default split range span is invalid.");
		return nullptr;
	}

	int64_t numberBase = getSecondaryRangeSplitNumberBase(mySQL);
	if (numberBase < 0)
	{
		LOG_FATAL("[Config ERROR] DBProxy secondary split number base is invalid.");
		return nullptr;
	}

	TableManagerBuilder builder(splitSpan, numberBase, utime);
	
	if (!fetchDBConfiguration_DatabaseInfo(mySQL, builder))
		return nullptr;
	if (!fetchDBConfiguration_TableInfo(mySQL, builder))
		return nullptr;
	if (!fetchDBConfiguration_SplitTableInfo(mySQL, builder))
		return nullptr;
	if (!fetchDBConfiguration_SplitRangeInfo(mySQL, builder))
		return nullptr;

	return builder.build(currentTableManager);
}

int64_t ConfigMonitor::getConfigurationUpdateTime(MySQLClient *mySQL)
{
	QueryResult queryResult;
	std::vector<std::vector<std::string>> &result = queryResult.rows;
	if (!mySQL->query(_cfgDBInfo.database, "select UNIX_TIMESTAMP(mtime) from variable_setting where name = 'DBProxy config data update'", queryResult))
		return 0;
		
	if (result.size())
	{
		return atoll(result[0][0].c_str());
	}	
	return 0;
}

int64_t ConfigMonitor::getSplitRangeSpan(MySQLClient *mySQL)
{
	QueryResult queryResult;
	std::vector<std::vector<std::string>> &result = queryResult.rows;
	if (!mySQL->query(_cfgDBInfo.database, "select value from variable_setting where name = 'default split range span'", queryResult))
		return -1;
		
	if (result.size())
	{
		return atoll(result[0][0].c_str());
	}	
	return -1;
}

int64_t ConfigMonitor::getSecondaryRangeSplitNumberBase(MySQLClient *mySQL)
{
	QueryResult queryResult;
	std::vector<std::vector<std::string>> &result = queryResult.rows;
	if (!mySQL->query(_cfgDBInfo.database, "select value from variable_setting where name = 'secondary split number base'", queryResult))
		return -1;
		
	if (result.size())
	{
		return atoi(result[0][0].c_str());
	}	
	return -1;
}

std::shared_ptr<MySQLClient> ConfigMonitor::createMySQLClient(int& host_index)
{
	std::shared_ptr<MySQLClient> my;
	while (host_index < (int)_cfgDBInfo.hosts.size())
	{
		my.reset(new MySQLClient(_cfgDBInfo.hosts[host_index], _cfgDBInfo.port, _cfgDBInfo.username, _cfgDBInfo.password, _cfgDBInfo.database, _cfgDBInfo.timeout));
		if (my->connected())
			return my;
		else
			host_index++;
	}
	
	throw FPNN_ERROR_MSG(InvalidConfigError, "NO available config database.");
}

void ConfigMonitor::monitor_thread()
{
	std::shared_ptr<TableManager> currentTableManager;
	int sync_tick = 0;
	int hostIndex = 0;
	
	while (true)
	try
	{
		recycleTableManagers();
		if (_willExit)
		{
			if (_recycledTableManagers.empty() && (!currentTableManager || currentTableManager->deletable()))
				return;
				
			sleep(3);
			continue;
		}
		
		std::shared_ptr<MySQLClient> mysql;
		
		int64_t new_update_time = 0;
		bool requireUpdate;
		{
			std::lock_guard<std::mutex> lck (_mutex);
			requireUpdate = _needRefresh;
			if (!currentTableManager)
				currentTableManager = _tableManager;
		}
	
		if (!requireUpdate)
		{
			if (!currentTableManager)
				requireUpdate = true;
			else if (sync_tick >= _cfgDBInfo.checkInterval)
			{
				if (!mysql)
					mysql = createMySQLClient(hostIndex);

				new_update_time = getConfigurationUpdateTime(mysql.get());
				if (new_update_time <= 0)
					throw FPNN_ERROR_FMT(InvalidConfigError, "Invalid config tables update time info at %s.", _cfgDBInfo.hosts[hostIndex].c_str());

				if (new_update_time > currentTableManager->updateTime())
					requireUpdate = true;

				sync_tick = 0;
			}
		}
		
		if (requireUpdate)
		{
			if (!mysql)
				mysql = createMySQLClient(hostIndex);

			TableManagerPtr tmp = initTableManager(mysql.get(), currentTableManager);
			if (tmp != nullptr)
			{
				{
					std::lock_guard<std::mutex> lck (_mutex);
					_tableManager = tmp;
					_needRefresh = false;
				}

				if (currentTableManager)
				{
					currentTableManager->signTakenOverTaskQueues(*tmp);
					_recycledTableManagers.push_back(currentTableManager);
				}

				currentTableManager = tmp;
				LOG_INFO("Load new table config info success. Database at index %d, addr: %s", hostIndex, _cfgDBInfo.hosts[hostIndex].c_str());
			}
			else
				throw FPNN_ERROR_FMT(InvalidConfigError, "Invalid config info at %s.", _cfgDBInfo.hosts[hostIndex].c_str());
		}

		sleep(3);
			
		sync_tick += 3;
		hostIndex = 0;
	}
	catch (const InvalidConfigError& e)
	{
		LOG_FATAL("EXCEPTION: [Config DB] %s", e.what());
		if (hostIndex >= (int)_cfgDBInfo.hosts.size())
		{
			sleep(2);

			sync_tick += 2;
			hostIndex = 0;
		}
	}
	catch (const std::exception& ex) //-- include FpnnError
	{
		LOG_ERROR("EXCEPTION: %s", ex.what());
		sleep(2);
			
		sync_tick += 2;
		hostIndex = 0;
	}
}

#include <sstream>
std::string ConfigMonitor::statusInJSON()
{
	std::shared_ptr<TableManager> currTableManager;
	std::list<std::shared_ptr<TableManager>> recyclingList;
	
	{
		std::lock_guard<std::mutex> lck (_mutex);
		currTableManager = _tableManager;
		recyclingList = _recycledTableManagers;
	}
	
	std::ostringstream oss;
	
	oss<<"{";
	{
		#ifdef DBProxy_Manager_Version
		oss<<"\"DBProxyType\":\"Standard Manager\"";
		#else
		oss<<"\"DBProxyType\":\"Standard\"";
		#endif
		oss<<",\"DBProxyVersion\":\"2.5.3\"";
		oss<<",\"recyclingQueueSize\":"<<recyclingList.size();
		oss<<",\"current\":"<<(currTableManager ? currTableManager->statusInJSON() : "{}");
		
		bool comma = false;
		oss<<",\"recycling\":";
		oss<<"[";
		for (std::list<std::shared_ptr<TableManager>>::iterator iter = recyclingList.begin(); iter != recyclingList.end(); iter++)
		{
			if (comma)
				oss<<",";
			else
				comma = true;
				
			oss<<(*iter)->statusInJSON();
		}
		oss<<"]";
	}
	oss<<"}";
	
	return oss.str();
}
