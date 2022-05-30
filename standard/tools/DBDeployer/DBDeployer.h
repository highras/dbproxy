#ifndef FPNN_DBProxy_Deployer_h
#define FPNN_DBProxy_Deployer_h

#include <map>
#include <memory>
#include <string>
#include <set>
#include "MySQLClient.h"

typedef std::shared_ptr<MySQLClient> MySQLClientPtr;

struct ConfigDatabase
{
	struct InstanceNode
	{
		std::string host;
		std::string port;
		std::string timeout;
		std::string user;
		std::string pwd;

		std::string toString()
		{
			return std::string(host).append(":").append(port);
		}
	};

	std::string _configDBName;
	std::map<int, InstanceNode> _instanceInfos;
	std::map<int, MySQLClientPtr> _masterInstances;
	std::set<int> _deployInstances;


	void reset(const std::string& dbname)
	{
		_configDBName = dbname;
		_instanceInfos.clear();
		_masterInstances.clear();
		_deployInstances.clear();
	}

	const std::string& dbName() { return _configDBName; }
	inline bool empty() { return _masterInstances.empty(); }
	MySQLClientPtr getMySQLClient(int server_id);
	std::string instanceEndpoint(int server_id);
	bool getBusinessAccount(int server_id, std::string& user, std::string& pwd);

	void addMasterDBClient(int server_id, const std::string& host, int port, const std::string& bizUser, const std::string& bizPwd, const std::string& adminUser, const std::string& adminPwd, int timeout);
	
	bool addDeployServer(int server_id);
	inline void removeDeployServer(int server_id) { _deployInstances.erase(server_id); }
	inline void clearDeployServer() { _deployInstances.clear(); }
	void showDeployServers();
};

struct Account
{
	std::string user;
	std::string pwd;
	bool remote;
	bool localhost;

	Account(): remote(true), localhost(true) {}
	Account(const std::string& user_, const std::string& pwd_): user(user_), pwd(pwd_), remote(true), localhost(true) {}
};

class DBDeployer
{
	Account _adminAccount;
	Account _confAccount;
	Account _dataAccount;
	MySQLClient _configDBClient;
	ConfigDatabase _configDB;

	bool mySQLIdleCheck(MySQLClient* mySQL);
	bool executeSQL(MySQLClient* mySQL, const std::string& database, const std::string& sql, const std::string& errorInfo);

	bool createDatabaseIfNotExit(MySQLClient* mySQLClient, const std::string& database);
	bool perpareDatabase(int deployServerId, const std::string& database);
	// bool perpareDatabases(MySQLClient* mySQLClient, const std::vector<std::string>& databases);

	bool checkAccount(MySQLClient* mySQLClient, const std::string& user, bool &localhost, bool &wildcard);
	bool createAccount(MySQLClient* mySQLClient, const std::string& user, const std::string& pwd, bool localhost, bool wildcard);
	bool configAccountOnInstance(MySQLClient* mySQLClient, const Account& account, const std::string& accountDescName);
	bool accountGrantOnDatabase(MySQLClient* mySQLClient, const std::string& database, const std::string& user,
		const std::string& pwd, const std::string& privileges, bool localhost, bool wildcard);

	//-- table operations
	void showTables(const std::string& sql);
	bool tableSplitType(const std::string& tableName, bool& splitByRange, int& hashTableCount);
	bool getRangeTableInfo(const std::string& tableName, std::string& databaseCategory,
		int64_t& span, bool& secondarySplit, int64_t& secondarySpan);
	int fetchSecondarySplitBase();
	int64_t fetchGlobalRangeSpan();
	void dropHashTable(const std::string& tableName, int tableCount);
	void dropHashTable(const std::string& tableName, const std::vector<std::string>& infos, bool split);
	void dropRangeTable(const std::string& tableName);

	bool insertHashTableInfo(const std::string& tableName, const std::string& database, int tableNumber, int serverId);

public:
	DBDeployer(const std::string &configDBHost, int configDBPort, const std::string &adminUser, const std::string &adminPwd):
		_adminAccount(adminUser, adminPwd), _configDBClient(configDBHost, configDBPort, adminUser, adminPwd) {}

	//-- database operations
	void showDatabases();
	void dropDatabase(const std::string& dbname);
	void createConfigDatabase(const std::string& dbname);
	void loadConfigDatabase(const std::string& dbname);

	//-- instance operations
	void showInstances(bool onlyMasterInstance);
	void addMySQLInstance(const std::string& host, int port, int timeoutInSeconds, bool master = true, int masterServerId = 0);

	//-- account operations
	void accountGrantOnConfigDatabase();
	void setConfAccount(const std::string& user, const std::string& pwd, bool localhost = true, bool remote = true)
	{
		_confAccount.user = user;
		_confAccount.pwd = pwd;
		_confAccount.localhost = localhost;
		_confAccount.remote = remote;
	}
	void setDataAccount(const std::string& user, const std::string& pwd, bool localhost = true, bool remote = true)
	{
		_dataAccount.user = user;
		_dataAccount.pwd = pwd;
		_dataAccount.localhost = localhost;
		_dataAccount.remote = remote;
	}

	//-- deploy servers operations
	void showDeployServers();
	void addDeployServer(int masterServerId);
	void removeDeployServer(int masterServerId);
	void clearDeployServers();

	//-- table operations
	void showAllTables();
	void showRangeTables();
	void showHashTables();
	void dropTable(const std::string& tableName);
	bool checkTableNameInCreateSQL(const std::string& tableName, const std::string& createSQL, bool silence = false);
	void createHashTable(const std::string& tableName, const std::string& createSQL, const std::string& hintField, int tableCount, const std::string& targetDatabase, int databaseCount);

	//-- misc operations
	void confuseConfigDatabase();
	void decodeConfigDatabase();
	void updateConfigTime();
};

#endif
