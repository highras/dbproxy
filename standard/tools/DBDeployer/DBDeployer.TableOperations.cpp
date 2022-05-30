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

using namespace std;
using namespace fpnn;

void DBDeployer::showTables(const std::string& sql)
{
	if (_configDB.dbName().empty())
	{
		cout<<"Please laod or create config database first."<<endl;
		return;
	}

	if (!mySQLIdleCheck(&_configDBClient))
		return;

	QueryResult result;
	if (_configDBClient.query(_configDB.dbName(), sql, result))
		printTable(result.fields, result.rows);
	else
		cout<<"select table table_info failed."<<endl;
}

void DBDeployer::showAllTables()
{
	showTables("select * from table_info");
}

void DBDeployer::showRangeTables()
{
	showTables("select id, table_name, range_span, database_category, secondary_split, secondary_split_span, hint_field from table_info where split_type = 1");
}

void DBDeployer::showHashTables()
{
	showTables("select id, table_name, table_count, hint_field from table_info where split_type = 0");
}

bool DBDeployer::tableSplitType(const std::string& tableName, bool& splitByRange, int& hashTableCount)
{
	if (_configDB.dbName().empty())
	{
		cout<<"Please laod or create config database first."<<endl;
		return false;
	}

	if (!mySQLIdleCheck(&_configDBClient))
		return false;

	std::string sql("select split_type, table_count from table_info where table_name = '");
	sql.append(tableName).append("'");

	QueryResult result;
	if (!_configDBClient.query(_configDB.dbName(), sql, result))
	{
		cout<<"select table table_info failed."<<endl;
		return false;
	}

	if (result.rows.empty())
	{
		cout<<"Cannot find table "<<tableName<<endl;
		return false;
	}

	splitByRange = atoi(result.rows[0][0].c_str());
	hashTableCount = atoi(result.rows[0][1].c_str());
	return true;
}

int DBDeployer::fetchSecondarySplitBase()
{
	QueryResult result;
	if (!_configDBClient.query(_configDB.dbName(), "select value from variable_setting where name = 'secondary split number base'", result))
	{
		cout<<"fetch 'secondary split number base' failed."<<endl;
		return -1;
	}

	if (result.rows.size())
		return atoi(result.rows[0][0].c_str());

	cout<<"fetch 'secondary split number base' failed."<<endl;
	return -1;
}

int64_t DBDeployer::fetchGlobalRangeSpan()
{
	QueryResult result;
	if (!_configDBClient.query(_configDB.dbName(), "select value from variable_setting where name = 'default split range span'", result))
	{
		cout<<"fetch 'default split range span' failed."<<endl;
		return 0;
	}

	if (result.rows.size())
		return atoll(result.rows[0][0].c_str());

	cout<<"fetch 'default split range span' failed."<<endl;
	return 0;
}

bool DBDeployer::getRangeTableInfo(const std::string& tableName, std::string& databaseCategory,
		int64_t& span, bool& secondarySplit, int64_t& secondarySpan)
{
	std::string sql("select range_span, database_category, secondary_split, secondary_split_span from table_info where table_name = '");
	sql.append(tableName).append("'");

	QueryResult result;
	if (!_configDBClient.query(_configDB.dbName(), sql, result))
	{
		cout<<"select range info from table table_info failed."<<endl;
		return false;
	}

	if (result.rows.empty())
	{
		cout<<"Cannot find table "<<tableName<<endl;
		return false;
	}

	span = atoll(result.rows[0][0].c_str());
	databaseCategory = result.rows[0][1];
	secondarySplit = atoi(result.rows[0][2].c_str());
	secondarySpan = atoll(result.rows[0][3].c_str());

	if (span == -1)
	{
		span = fetchGlobalRangeSpan();
		if (span <= 0)
			return false;
	}

	return true;
}

void DBDeployer::dropHashTable(const std::string& tableName, const std::vector<std::string>& infos, bool split)
{
	std::string sql("drop table ");
	sql.append(tableName);

	if (split)
		sql.append("_").append(infos[0]);

	std::string errorInfo(sql);
	errorInfo.append(" failed.");

	int server_id = atoi(infos[1].c_str());
	MySQLClientPtr client = _configDB.getMySQLClient(server_id);
	if (!client)
		cout<<"Cannot find master instance of server id "<<server_id<<"."<<endl;
	else
		executeSQL(client.get(), infos[2], sql, errorInfo);
}

void DBDeployer::dropHashTable(const std::string& tableName, int tableCount)
{
	std::string loadTablesSQL("select table_number, server_id, database_name from split_table_info where table_name = '");
	loadTablesSQL.append(tableName).append("'");

	QueryResult result;
	if (!_configDBClient.query(_configDB.dbName(), loadTablesSQL, result))
	{
		cout<<"fetch table distribution info failed."<<endl;
		return;
	}

	if (tableCount <= 1)
	{
		if (result.rows.size())
			dropHashTable(tableName, result.rows[0], false);
	}
	else
	{
		for (auto& row: result.rows)
			dropHashTable(tableName, row, true);
	}

	//-- clear table info
	{
		std::string sql("delete from table_info where table_name = '");
		sql.append(tableName).append("'");

		executeSQL(&_configDBClient, _configDB.dbName(), sql, "clear table_info failed.");
	}

	//-- clear split table info
	{
		std::string sql("delete from split_table_info where table_name = '");
		sql.append(tableName).append("'");

		executeSQL(&_configDBClient, _configDB.dbName(), sql, "clear split_table_info failed.");
	}
}

void DBDeployer::dropRangeTable(const std::string& tableName)
{
	bool secondarySplit;
	std::string databaseCategory;
	int64_t span, secondarySpan;

	if (!getRangeTableInfo(tableName, databaseCategory, span, secondarySplit, secondarySpan))
		return;

	int secondaryBase = 0;
	int secondaryCount = 0;
	if (secondarySplit)
	{
		secondaryBase = fetchSecondarySplitBase();
		if (secondaryBase == -1)
			return;

		secondaryCount = span / secondarySpan;
		if (secondaryCount * secondarySpan < span)
			secondaryCount += 1;
	}

	struct DistributionInfo
	{
		int server_id;
		int rangeIdx;
		std::string database;
		std::set<std::string> tableNames;
	};

	std::vector<DistributionInfo> distributionInfos;

	{
		std::string sql("select split_index, database_name, server_id from split_range_info where database_category = '");
		sql.append(databaseCategory).append("'");

		QueryResult result;
		if (!_configDBClient.query(_configDB.dbName(), sql, result))
		{
			cout<<"select range category info from table split_range_info failed."<<endl;
			return;
		}

		for (auto& row: result.rows)
		{
			struct DistributionInfo disInfo;
			disInfo.server_id = atoi(row[2].c_str());
			disInfo.database = row[1];

			disInfo.rangeIdx = atoi(row[0].c_str());

			if (span == 0)
			{
				if (disInfo.rangeIdx)
					continue;

				disInfo.tableNames.insert(tableName);
				distributionInfos.push_back(disInfo);
			}
			else
			{
				if (secondarySplit)
				{
					for (int i = secondaryBase; i < secondaryCount + secondaryBase; i++)
					{
						std::string name(tableName);
						name.append(std::to_string(i));
						disInfo.tableNames.insert(name);
					}
				}
				else
					disInfo.tableNames.insert(tableName);

				distributionInfos.push_back(disInfo);
			}
		}
	}
	
	for (auto& info: distributionInfos)
	{
		MySQLClientPtr client = _configDB.getMySQLClient(info.server_id);
		if (!client)
		{
			cout<<"Cannot find master instance of server id "<<info.server_id<<"."<<endl;
			continue;
		}

		for (auto& name: info.tableNames)
		{
			std::string sql("drop table ");
			sql.append(name);

			std::string errorInfo(sql);
			errorInfo.append(" in range ").append(std::to_string(info.rangeIdx)).append(" failed.");

			executeSQL(client.get(), info.database, sql, errorInfo);
		}
	}

	{
		std::string sql("delete from table_info where table_name = '");
		sql.append(tableName).append("'");

		executeSQL(&_configDBClient, _configDB.dbName(), sql, "clear table_info failed.");
	}
}

void DBDeployer::dropTable(const std::string& tableName)
{
	bool splitByRange;
	int hashTableCount;
	if (tableSplitType(tableName, splitByRange, hashTableCount) == false)
		return;

	if (splitByRange)
		dropRangeTable(tableName);
	else
		dropHashTable(tableName, hashTableCount);
}

bool DBDeployer::checkTableNameInCreateSQL(const std::string& tableName, const std::string& createSQL, bool silence)
{
	const char* createHeader = "CREATE TABLE ";
	const char* optFollower = "IF NOT EXISTS ";

	std::string orgSQL = createSQL;
	std::string sql = StringUtil::trim(orgSQL);

	size_t headerLen = strlen(createHeader);
	size_t followerLen = strlen(optFollower);

	if (strncasecmp(createHeader, sql.c_str(), headerLen) != 0)
	{
		if (!silence)
		{
			cout<<"SQL is not leading by 'CREATE TABLE '"<<endl;
			cout<<"Original SQL is:"<<endl;
			cout<<createSQL<<endl<<endl;
		}
		return false;
	}

	char* pos = (char *)sql.c_str() + headerLen;
	if (strncasecmp(optFollower, sql.c_str() + headerLen, followerLen) == 0)
		pos += followerLen;

	static StringUtil::CharsChecker charChecker(" `\'\r\n\t");
	static StringUtil::CharsChecker charChecker2(" `\'\r\n\t(");

	while (charChecker[*pos])
		pos += 1;

	if (strncmp(pos, tableName.c_str(), tableName.length()) != 0)
	{
		if (!silence)
		{
			cout<<"Cannot find table name: '"<<tableName<<"' in SQL:"<<endl;
			cout<<createSQL<<endl<<endl;
		}
		return false;
	}

	char c = *(pos + tableName.length());
	if (!charChecker2[c])
	{
		if (!silence)
		{
			cout<<"Cannot find table name: '"<<tableName<<"' in SQL:"<<endl;
			cout<<createSQL<<endl<<endl;
		}
		return false;
	}

	return true;
}

bool DBDeployer::insertHashTableInfo(const std::string& tableName, const std::string& database, int tableNumber, int serverId)
{
	std::ostringstream ossSQL;
	ossSQL<<"insert into split_table_info (table_name, table_number, server_id, database_name) values ('";
	ossSQL<<tableName<<"', "<<tableNumber<<", "<<serverId<<", '"<<database<<"')";

	std::ostringstream ossInfo;
	ossInfo<<"Insert into split_table_info for table "<<tableName<<" number "<<tableNumber<<" in database "<<database<<" failed.";

	return executeSQL(&_configDBClient, _configDB.dbName(), ossSQL.str(), ossInfo.str());
}

void DBDeployer::createHashTable(const std::string& tableName, const std::string& createSQL, const std::string& hintField, int tableCount, const std::string& targetDatabase, int databaseCount)
{
	//-- check table name match
	if (tableCount < 1)
	{
		cout<<"Table count error!"<<endl;
		return;
	}

	if (!checkTableNameInCreateSQL(tableName, createSQL))
		return;

	if (_configDB._deployInstances.empty())
	{
		cout<<"Please config deploy servers at first."<<endl;
		return;
	}

	//-- perpare database distribution
	struct DBInfo
	{
		int serverId;
		std::string databaseName;
	};
	std::map<int, struct DBInfo> databaseDistribution;
	{
		std::vector<int> deployServerIds;
		for (int serverId: _configDB._deployInstances)
			deployServerIds.push_back(serverId);

		if (databaseCount == 1)
		{
			databaseDistribution[0].databaseName = targetDatabase;
			databaseDistribution[0].serverId = deployServerIds[0];
		}
		else
		{
			size_t deployIdx = 0;
			for (int i = 0; i < databaseCount; i++)
			{
				std::string realDatabaseName(targetDatabase);
				realDatabaseName.append("_").append(std::to_string(i));

				databaseDistribution[i].databaseName = realDatabaseName;
				databaseDistribution[i].serverId = deployServerIds[deployIdx];
				deployIdx += 1;

				if (deployIdx == deployServerIds.size())
					deployIdx = 0;
			}
		}
	}

	//-- perpare table distribution
	std::map<int, int> tableDistribution;	//-- map<table index, database id>
	{
		int dbIdx = 0;
		for (int i = 0; i < tableCount; i++)
		{
			tableDistribution[i] = dbIdx;
			dbIdx += 1;

			if (dbIdx == databaseCount)
				dbIdx = 0;
		}
	}

	//-- prepate targerDatabase
	{
		if (tableCount == 1)
		{
			int deployServerId = databaseDistribution[0].serverId;
			if (!perpareDatabase(deployServerId, targetDatabase))
			{
				cout<<"Perpare database "<<targetDatabase<<" on "<<_configDB.instanceEndpoint(deployServerId)<<" failed."<<endl;
				return;
			}
		}
		else
		{
			std::map<int, std::set<int>> sidDid;		//-- map<serverId, set<database id>>

			for (auto& ddp: databaseDistribution)
				sidDid[ddp.second.serverId].insert(ddp.first);

			for (auto& sdp: sidDid)
			{
				int deployServerId = sdp.first;
				for (int dbIdx: sdp.second)
				{
					std::string& databaseName = databaseDistribution[dbIdx].databaseName;

					if (!perpareDatabase(deployServerId, databaseName))
					{
						cout<<"Perpare database "<<databaseName<<" on "<<_configDB.instanceEndpoint(deployServerId)<<" failed."<<endl;
						return;
					}
				}
			}		
		}
	}

	//-- fill table_info
	{
		std::ostringstream oss;
		oss<<"insert into table_info (table_name, split_type, table_count, hint_field) values ('";
		oss<<tableName<<"', 0, "<<tableCount<<", '"<<hintField<<"')";

		std::string sql = oss.str();

		if (!executeSQL(&_configDBClient, _configDB.dbName(), sql, "insert into table_info failed."))
			return;
	}

	//-- create table
	if (tableCount == 1)
	{
		int deployServerId = databaseDistribution[0].serverId;
		MySQLClientPtr mySQLClient = _configDB.getMySQLClient(deployServerId);

		std::ostringstream oss;
		oss<<"Create table "<<tableName<<" in database "<<targetDatabase<<" on ";
		oss<<_configDB.instanceEndpoint(deployServerId)<<" failed.";
		
		std::string errorInfo = oss.str();
		if (!executeSQL(mySQLClient.get(), targetDatabase, createSQL, errorInfo))
			return;

		if (!insertHashTableInfo(tableName, targetDatabase, 0, deployServerId))
			return;
	}
	else
	{
		for (int i = 0; i < tableCount; i++)
		{
			int databaseIdx = tableDistribution[i];
			int deployServerId = databaseDistribution[databaseIdx].serverId;
			MySQLClientPtr mySQLClient = _configDB.getMySQLClient(deployServerId);

			std::string& databaseName = databaseDistribution[databaseIdx].databaseName;

			std::string suffix("_");
			suffix.append(std::to_string(i));

			std::string realSQL(createSQL);
			size_t found = realSQL.find(tableName);
			realSQL.insert(found + tableName.length(), suffix);

			std::ostringstream oss;
			oss<<"Create table "<<tableName<<suffix<<" in database "<<databaseName<<" on ";
			oss<<_configDB.instanceEndpoint(deployServerId)<<" failed.";
			
			std::string errorInfo = oss.str();			
			if (!executeSQL(mySQLClient.get(), databaseName, realSQL, errorInfo))
				return;

			if (!insertHashTableInfo(tableName, databaseName, i, deployServerId))
				return;
		}
	}

	cout<<"Create completed."<<endl;
}
