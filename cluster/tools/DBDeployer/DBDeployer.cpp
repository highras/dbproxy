#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <vector>
#include <set>
#include <sstream>
#include "FormattedPrint.h"
#include "StringUtil.h"
#include "DBDeployer.h"
#include "createConfigDatabaseSQLs.inc"

using namespace std;
using namespace fpnn;

MySQLClientPtr ConfigDatabase::getMySQLClient(int server_id)
{
	auto iter = _masterInstances.find(server_id);
	if (iter != _masterInstances.end())
		return iter->second;
	else
		return nullptr;
}

std::string ConfigDatabase::instanceEndpoint(int server_id)
{
	auto iter = _instanceInfos.find(server_id);
	if (iter != _instanceInfos.end())
		return iter->second.toString();
	else
		return "";
}

bool ConfigDatabase::getBusinessAccount(int server_id, std::string& user, std::string& pwd)
{
	auto iter = _instanceInfos.find(server_id);
	if (iter != _instanceInfos.end())
	{
		user = iter->second.user;
		pwd = iter->second.pwd;
		return true;
	}
	else
		return false;
}

void ConfigDatabase::addMasterDBClient(int server_id, const std::string& host, int port, const std::string& bizUser, const std::string& bizPwd, const std::string& adminUser, const std::string& adminPwd, int timeout)
{
	MySQLClientPtr client(new MySQLClient(host, port, adminUser, adminPwd, "", timeout));
	_masterInstances[server_id] = client;

	InstanceNode node{host, std::to_string(port), std::to_string(timeout), bizUser, bizPwd};
	_instanceInfos[server_id] = node;
}

bool ConfigDatabase::addDeployServer(int server_id)
{
	auto iter = _masterInstances.find(server_id);
	if (iter != _masterInstances.end())
	{
		_deployInstances.insert(server_id);
		return true;
	}
	else
		return false;
}

void ConfigDatabase::showDeployServers()
{
	const std::vector<std::string> fields{"server id", "host", "port", "timeout"};

	std::vector<std::vector<std::string>> rows;
	for (int serverId: _deployInstances)
	{
		std::vector<std::string> row;

		row.push_back(std::to_string(serverId));
		row.push_back(_instanceInfos[serverId].host);
		row.push_back(_instanceInfos[serverId].port);
		row.push_back(_instanceInfos[serverId].timeout);

		rows.push_back(row);
	}

	printTable(fields, rows);
}

bool DBDeployer::mySQLIdleCheck(MySQLClient* mySQL)
{
	const int mySQLRepingInterval = 5 * 60;

	bool connected = mySQL->connected();
	if (connected)
	{
		if (time(NULL) - mySQL->lastOperatedTime() >= mySQLRepingInterval)
			connected = mySQL->ping();
	}
	if (!connected)
	{
		mySQL->cleanup();
		if (!mySQL->connect())
		{
			cout<<"Database connection lost. Reconnect failed."<<endl;
			return false;
		}
	}
	return true;
}

void DBDeployer::showDatabases()
{
	if (!mySQLIdleCheck(&_configDBClient))
		return;

	QueryResult result;
	if (_configDBClient.query("", "show databases", result))
		printTable(result.fields, result.rows);
	else
		cout<<"show databases failed."<<endl;
}

void DBDeployer::dropDatabase(const std::string& dbname)
{
	if (!mySQLIdleCheck(&_configDBClient))
		return;

	std::string sql("drop database ");
	sql.append(dbname);

	QueryResult result;
	if (_configDBClient.query("", sql, result) == false)
		cout<<sql<<" failed."<<endl;

	if (dbname == _configDB.dbName())
		_configDB.reset("");
}

bool DBDeployer::executeSQL(MySQLClient* mySQL, const std::string& database, const std::string& sql, const std::string& errorInfo)
{
	QueryResult result;
	if (mySQL->query(database, sql, result) == false)
	{
		cout<<errorInfo<<endl;
		return false;
	}
	return true;
}

void DBDeployer::createConfigDatabase(const std::string& dbname)
{
	if (!mySQLIdleCheck(&_configDBClient))
		return;

	std::string createSQL_configDB("CREATE DATABASE IF NOT EXISTS ");
	createSQL_configDB.append(dbname).append(" CHARACTER SET utf8");

	if (executeSQL(&_configDBClient, "", createSQL_configDB, "create database failed.") == false)
		return;

	if (executeSQL(&_configDBClient, dbname, createSQL_serverInfo, "create table server_info failed.") == false)
		return;
	if (executeSQL(&_configDBClient, dbname, createSQL_tableInfo, "create table table_info failed.") == false)
		return;
	if (executeSQL(&_configDBClient, dbname, createSQL_splitTableInfo, "create table split_table_info failed.") == false)
		return;
	if (executeSQL(&_configDBClient, dbname, createSQL_splitRangeInfo, "create table split_range_info failed.") == false)
		return;
	if (executeSQL(&_configDBClient, dbname, createSQL_variableSetting, "create table variable_setting failed.") == false)
		return;

	std::vector<std::string> initSqlArray;
	StringUtil::split(initSQLs, "\r\n", initSqlArray);

	//cout<<initSqlArray.size()<<" sqls will excuted."<<endl;
	
	for (std::string& sql: initSqlArray)
	{
		std::string errInfo("execute SQL \"");
		errInfo.append(sql).append("\" failed.");

		if (executeSQL(&_configDBClient, dbname, sql, errInfo) == false)
			return;
	}

	_configDB.reset(dbname);
	cout<<"Create completed!"<<endl;
}

void DBDeployer::loadConfigDatabase(const std::string& dbname)
{
	const char* sql = "select server_id, host, port, user, passwd, timeout from server_info where master_sid = 0";

	if (!mySQLIdleCheck(&_configDBClient))
		return;

	QueryResult result;
	if (_configDBClient.query(dbname, sql, result) == false)
	{
		cout<<"Load table server_info failed."<<endl;
		return;
	}

	_configDB.reset(dbname);

	for (size_t i = 0; i < result.rows.size(); i++)
	{
		std::string host = result.rows[i][1];
		int port = atoi(result.rows[i][2].c_str());

		_configDB.addMasterDBClient(atoi(result.rows[i][0].c_str()), host, port, result.rows[i][3], result.rows[i][4],
			_adminAccount.user, _adminAccount.pwd, atoi(result.rows[i][5].c_str()));

		cout << "load master instance " << host << ":" << port << endl;
	}

	cout<<endl<<"Load "<<result.rows.size()<<" master instances."<<endl;
}

bool DBDeployer::createDatabaseIfNotExit(MySQLClient* mySQLClient, const std::string& database)
{
	if (!mySQLIdleCheck(mySQLClient))
		return false;

	std::string sql("CREATE DATABASE IF NOT EXISTS ");
	sql.append(database).append(" CHARACTER SET utf8");

	return executeSQL(mySQLClient, "", sql, "create database failed.");
}

bool DBDeployer::perpareDatabase(int deployServerId, const std::string& database)
{
	MySQLClient* mySQLClient = _configDB.getMySQLClient(deployServerId).get();

	Account account;
	_configDB.getBusinessAccount(deployServerId, account.user, account.pwd);

	if (createDatabaseIfNotExit(mySQLClient, database) == false)
		return false;

	if (configAccountOnInstance(mySQLClient, account, "business") == false)
		return false;

	return accountGrantOnDatabase(mySQLClient, database, account.user, account.pwd, "select, update, insert, delete", account.localhost, account.remote);
}

void DBDeployer::showInstances(bool onlyMasterInstance)
{
	if (_configDB.dbName().empty())
	{
		cout<<"Please laod config database first."<<endl;
		return;
	}

	std::string sql("select * from server_info");
	if (onlyMasterInstance)
		sql.append(" where master_sid = 0");
	
	if (!mySQLIdleCheck(&_configDBClient))
		return;

	QueryResult result;
	if (_configDBClient.query(_configDB.dbName(), sql, result))
		printTable(result.fields, result.rows);
	else
		cout<<"select table server_info failed."<<endl;
}

void DBDeployer::addMySQLInstance(const std::string& host, int port, int timeoutInSeconds, bool master, int masterServerId)
{
	if (_configDB.dbName().empty())
	{
		cout<<"Please laod config database first."<<endl;
		return;
	}

	if (_dataAccount.user.empty())
	{
		cout<<"Please config business account first."<<endl;
		return;
	}

	if (master)
		masterServerId = 0;

	if (!mySQLIdleCheck(&_configDBClient))
		return;

	std::ostringstream oss;
	oss<<"insert into server_info (master_sid, host, port, user, passwd, timeout) values (";
	oss<<masterServerId<<", '"<<host<<"', "<<port<<", '"<<_dataAccount.user<<"', '"<<_dataAccount.pwd<<"', ";
	oss<<timeoutInSeconds<<")";

	std::string sql = oss.str();

	if (executeSQL(&_configDBClient, _configDB.dbName(), sql, "add new instance failed.") == false)
		return;

	if (master)
	{
		std::string configDBName = _configDB.dbName();
		loadConfigDatabase(configDBName);

		//for (int sid: _configDB._deployInstances)
		//	_configDB.addDeployServer(sid);
	}

	cout<<"Add instance completed."<<endl;
}

bool DBDeployer::checkAccount(MySQLClient* mySQLClient, const std::string& user, bool &localhost, bool &wildcard)
{
	wildcard = false;
	localhost = false;

	if (!mySQLIdleCheck(mySQLClient))
		return false;

	std::string checkSQL("select Host, User from user where User = '");
	checkSQL.append(user).append("'");

	QueryResult result;
	if (mySQLClient->query("mysql", checkSQL, result) == false)
	{
		cout<<"select table user in database mysql failed."<<endl;
		return false;
	}

	for (auto& row: result.rows)
	{
		if (row[0] == "%")
			wildcard = true;
		else if (row[0] == "localhost")
			localhost = true;
	}

	return true;
}

bool DBDeployer::createAccount(MySQLClient* mySQLClient, const std::string& user, const std::string& pwd, bool localhost, bool wildcard)
{
	if (!mySQLIdleCheck(mySQLClient))
		return false;

	if (localhost)
	{
		std::string sql("CREATE USER '");
		sql.append(user).append("'@'localhost' IDENTIFIED BY '").append(pwd).append("'");

		if (executeSQL(mySQLClient, "", sql, "create user for localhost failed.") == false)
			return false;
	}

	if (wildcard)
	{
		std::string sql("CREATE USER '");
		sql.append(user).append("'@'%' IDENTIFIED BY '").append(pwd).append("'");

		if (executeSQL(mySQLClient, "", sql, "create user for any remote addresses failed.") == false)
			return false;
	}

	if (executeSQL(mySQLClient, "", "flush privileges", "flush privileges failed.") == false)
		return false;

	return true;
}

bool DBDeployer::configAccountOnInstance(MySQLClient* mySQLClient, const Account& account, const std::string& accountDescName)
{
	if (account.user.empty())
	{
		cout<<"Please config "<<accountDescName<<" account first."<<endl;
		return false;
	}

	bool localhost, wildcard;
	if (checkAccount(mySQLClient, account.user, localhost, wildcard) == false)
	{
		cout<<"Check "<<accountDescName<<" account at "<<mySQLClient->endpoint()<<" failed."<<endl;
		return false;
	}
	
	if (!localhost && account.localhost)
		localhost = true;
	else
		localhost = false;

	if (!wildcard && account.remote)
		wildcard = true;
	else
		wildcard = false;

	if (localhost && wildcard)
		if (createAccount(mySQLClient, account.user, account.pwd, localhost, wildcard) == false)
		{
			cout<<"Create "<<accountDescName<<" account at "<<mySQLClient->endpoint()<<" failed."<<endl;
			return false;
		}

	return true;
}

bool DBDeployer::accountGrantOnDatabase(MySQLClient* mySQLClient, const std::string& database, const std::string& user,
		const std::string& pwd, const std::string& privileges, bool localhost, bool wildcard)
{
	if (!mySQLIdleCheck(mySQLClient))
		return false;

	if (localhost)
	{
		std::string sql("GRANT ");
		sql.append(privileges).append(" ON ").append(database).append(".* TO '");
		sql.append(user).append("'@'localhost' IDENTIFIED BY '").append(pwd).append("'");

		if (executeSQL(mySQLClient, "", sql, "grant privileges to user for localhost failed.") == false)
			return false;	
	}

	if (wildcard)
	{
		std::string sql("GRANT ");
		sql.append(privileges).append(" ON ").append(database).append(".* TO '");
		sql.append(user).append("'@'%' IDENTIFIED BY '").append(pwd).append("'");

		if (executeSQL(mySQLClient, "", sql, "grant privileges to user for any remote addresses failed.") == false)
			return false;
	}

	return executeSQL(mySQLClient, "", "flush privileges", "flush privileges failed.");
}

void DBDeployer::accountGrantOnConfigDatabase()
{
	if (_configDB.dbName().empty())
	{
		cout<<"Please laod or create config database first."<<endl;
		return;
	}

	if (configAccountOnInstance(&_configDBClient, _confAccount, "DBProxy") == false)
	{
		cout<<"Config DBProxy account failed."<<endl;
		return;
	}

	accountGrantOnDatabase(&_configDBClient, _configDB.dbName(), _confAccount.user, _confAccount.pwd, "select", _confAccount.localhost, _confAccount.remote);
}

void DBDeployer::showDeployServers()
{
	if (_configDB.dbName().empty())
	{
		cout<<"Please laod or create config database first."<<endl;
		return;
	}

	_configDB.showDeployServers();
}
void DBDeployer::addDeployServer(int masterServerId)
{
	if (_configDB.dbName().empty())
	{
		cout<<"Please laod or create config database first."<<endl;
		return;
	}

	if (_configDB.addDeployServer(masterServerId) == 0)
		cout<<"Cannot find server with server_id "<<masterServerId<<endl;
}
void DBDeployer::removeDeployServer(int masterServerId)
{
	if (_configDB.dbName().empty())
	{
		cout<<"Please laod or create config database first."<<endl;
		return;
	}

	_configDB.removeDeployServer(masterServerId);
}
void DBDeployer::clearDeployServers()
{
	if (_configDB.dbName().empty())
	{
		cout<<"Please laod or create config database first."<<endl;
		return;
	}

	_configDB.clearDeployServer();
}

void DBDeployer::updateConfigTime()
{
	if (_configDB.dbName().empty())
	{
		cout<<"Please laod or create config database first."<<endl;
		return;
	}

	if (!mySQLIdleCheck(&_configDBClient))
		return;

	if (executeSQL(&_configDBClient, _configDB.dbName(), "update variable_setting set mtime = NOW() where name = 'DBProxy config data update'", "update config time failed."))
		cout<<"Updated."<<endl;
}
