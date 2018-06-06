#include <iostream>
#include <sstream>
#include "StringUtil.h"
#include "FileSystemUtil.h"
#include "ignoreSignals.h"
#include "linenoise.h"
#include "CommandProcessor.h"
#include "DBDeployer.h"

using namespace std;
using namespace fpnn;

void showHelp()
{
	cout<<"---- database operations ----"<<endl;
	cout<<"  show db/dbs/database/databases"<<endl;
	cout<<"  drop database <database_name>"<<endl;
	cout<<"  create config db/database <database_name>"<<endl;
	cout<<"  load config db/database <database_name>"<<endl;
	cout<<"  grant config db/database"<<endl;
	cout<<endl;
	cout<<"---- instance operations ----"<<endl;
	cout<<"  show all instances"<<endl;
	cout<<"  show master instances"<<endl;
	cout<<"  add master instance <host> <port> [timeout_in_sec]"<<endl;
	cout<<"  add slave instance <host> <port> <master_server_id> [timeout_in_sec]"<<endl;
	cout<<endl;
	cout<<"---- deploy target server operations ----"<<endl;
	cout<<"  show deploy servers"<<endl;
	cout<<"  add deploy server <master_server_id>"<<endl;
	cout<<"  remove deploy server <master_server_id>"<<endl;
	cout<<"  clear deploy servers"<<endl;
	cout<<endl;
	cout<<"---- account operations ----"<<endl;
	cout<<"  config DBProxy account <user_name> <password> [options]"<<endl;
	cout<<"  config business account <user_name> <password> [options]"<<endl;
	cout<<"      options:"<<endl;
	cout<<"          only remote"<<endl;
	cout<<"          only localhost"<<endl;
	cout<<endl;
	cout<<"---- table operations ----"<<endl;
	cout<<"  show all tables [cluster]"<<endl;
	cout<<"  show hash tables [cluster]"<<endl;
	cout<<"  show range tables [cluster]"<<endl;
	cout<<"  drop table <table_name> [cluster]"<<endl;
	cout<<"  add hash table <table_name> <table_count> [with hintId field <fiedl_name>] [to cluster <cluster_name>] in database <basic_database_name> <database_count>"<<endl;
	cout<<"  add hash table from <sql_file_path> <table_name> <table_count> [with hintId field <fiedl_name>] [to cluster <cluster_name>] in database <basic_database_name> <database_count>"<<endl;
	cout<<endl;
	cout<<"---- misc operations ----"<<endl;
	cout<<"  confuse config db/database"<<endl;
	cout<<"  decode config db/database"<<endl;
	cout<<"  update config time"<<endl;
	cout<<endl;
	cout<<"  help"<<endl;
	cout<<"  exit"<<endl;
	cout<<"  quit"<<endl;
	cout<<endl;
}

void initDatabaseOperations(CommandProcessor& processor, DBDeployer& deployer)
{
	//------- show databases ------------//
	{
		CmdExecutorPtr executor(new std::function<void (int paramsBeginIdx, const std::vector<std::string>& cmd)>(
			[&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) { deployer.showDatabases(); }
			));

		processor.registerCmd("show databases", executor);
		processor.registerCmd("show database", executor);
		processor.registerCmd("show db", executor);
		processor.registerCmd("show dbs", executor);
	}

	//------- drop database ------------//
	processor.registerCmd("drop database", [&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) {
		if (cmd.size() != 3)
		{
			cout<<"Parameters error!"<<endl;
			return;
		}
		deployer.dropDatabase(cmd[2]);
		cout<<"Drop completed!"<<endl;
	});

	//------- create config database ------------//
	{
		CmdExecutorPtr executor(new std::function<void (int paramsBeginIdx, const std::vector<std::string>& cmd)>(
			[&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd)
			{
				if (cmd.size() != 4)
				{
					cout<<"Parameters error!"<<endl;
					return;
				}
				deployer.createConfigDatabase(cmd[3]);
			}
			));

		processor.registerCmd("create config db", executor);
		processor.registerCmd("create config database", executor);
	}

	//------- load config database ------------//
	{
		CmdExecutorPtr executor(new std::function<void (int paramsBeginIdx, const std::vector<std::string>& cmd)>(
			[&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd)
			{
				if (cmd.size() != 4)
				{
					cout<<"Parameters error!"<<endl;
					return;
				}
				deployer.loadConfigDatabase(cmd[3]);
			}
			));

		processor.registerCmd("load config db", executor);
		processor.registerCmd("load config database", executor);
	}

	//------- grant config db/database ------------//
	{
		CmdExecutorPtr executor(new std::function<void (int paramsBeginIdx, const std::vector<std::string>& cmd)>(
			[&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd)
			{
				if (cmd.size() != 3)
				{
					cout<<"Parameters error!"<<endl;
					return;
				}
				deployer.accountGrantOnConfigDatabase();
			}
			));

		processor.registerCmd("grant config db", executor);
		processor.registerCmd("grant config database", executor);
	}
}

void initInstanceOperations(CommandProcessor& processor, DBDeployer& deployer)
{
	processor.registerCmd("show all instances", [&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) {
		if (cmd.size() != 3)
		{
			cout<<"Parameters error!"<<endl;
			return;
		}
		deployer.showInstances(false);
	});

	processor.registerCmd("show master instances", [&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) {
		if (cmd.size() != 3)
		{
			cout<<"Parameters error!"<<endl;
			return;
		}
		deployer.showInstances(true);
	});

	processor.registerCmd("add master instance", [&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) {
		if (cmd.size() != 5 && cmd.size() != 6)
		{
			cout<<"Parameters error!"<<endl;
			return;
		}

		int timeout = 0;
		if (cmd.size() == 6)
			timeout = atoi(cmd[5].c_str());

		deployer.addMySQLInstance(cmd[3], atoi(cmd[4].c_str()), timeout, true, 0);
	});

	processor.registerCmd("add slave instance", [&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) {
		if (cmd.size() != 6 && cmd.size() != 7)
		{
			cout<<"Parameters error!"<<endl;
			return;
		}

		int timeout = 0;
		if (cmd.size() == 7)
			timeout = atoi(cmd[6].c_str());

		deployer.addMySQLInstance(cmd[3], atoi(cmd[4].c_str()), timeout, false, atoi(cmd[5].c_str()));
	});
}

void initDeployServerOperations(CommandProcessor& processor, DBDeployer& deployer)
{
	processor.registerCmd("show deploy servers", [&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) {
		if (cmd.size() != 3)
		{
			cout<<"Parameters error!"<<endl;
			return;
		}
		deployer.showDeployServers();
	});

	processor.registerCmd("add deploy server", [&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) {
		if (cmd.size() != 4)
		{
			cout<<"Parameters error!"<<endl;
			return;
		}
		deployer.addDeployServer(atoi(cmd[3].c_str()));
	});

	processor.registerCmd("remove deploy server", [&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) {
		if (cmd.size() != 4)
		{
			cout<<"Parameters error!"<<endl;
			return;
		}
		deployer.removeDeployServer(atoi(cmd[3].c_str()));
	});

	processor.registerCmd("clear deploy servers", [&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) {
		if (cmd.size() != 3)
		{
			cout<<"Parameters error!"<<endl;
			return;
		}
		deployer.clearDeployServers();
	});
}

void initAccountOperations(CommandProcessor& processor, DBDeployer& deployer)
{
	processor.registerCmd("config DBProxy account", [&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) {
		if (cmd.size() == 5)
		{
			deployer.setConfAccount(cmd[3], cmd[4]);
			cout<<"Config completed."<<endl;
		}
		else if (cmd.size() == 7 && cmd[5] == "only" && (cmd[6] == "remote" || cmd[6] == "localhost"))
		{
			bool localhost = true, remote = true;
			if (cmd[6] == "remote")
				localhost = false;
			else
				remote = false;

			deployer.setConfAccount(cmd[3], cmd[4], localhost, remote);
			cout<<"Config completed."<<endl;
		}
		else
			cout<<"Parameters error!"<<endl;
	});

	processor.registerCmd("config business account", [&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) {
		if (cmd.size() == 5)
		{
			deployer.setDataAccount(cmd[3], cmd[4]);
			cout<<"Config completed."<<endl;
		}
		else if (cmd.size() == 7 && cmd[5] == "only" && (cmd[6] == "remote" || cmd[6] == "localhost"))
		{
			bool localhost = true, remote = true;
			if (cmd[6] == "remote")
				localhost = false;
			else
				remote = false;

			deployer.setDataAccount(cmd[3], cmd[4], localhost, remote);
			cout<<"Config completed."<<endl;
		}
		else
			cout<<"Parameters error!"<<endl;
	});
}

class CommandLineFetcher
{
	std::string _prompt;

protected:
	virtual bool processLine(std::string& line) = 0;

public:
	CommandLineFetcher(const std::string& prompt): _prompt(prompt) {}
	void runLoop()
	{
		char *rawline;
		while ((rawline = linenoise(_prompt.c_str())) != NULL)
		{
			linenoiseHistoryAdd(rawline);
			linenoiseHistorySave(".dbdeployer.cmd.log");
			char *l = StringUtil::trim(rawline);
			std::string line(l);
			free(rawline);

			if (processLine(line))
				continue;

			return;
		}
	}
};

class SQLFetcher: public CommandLineFetcher
{
	std::ostringstream _oss;

protected:
	virtual bool processLine(std::string& line)
	{
		if (line.empty())
			return true;

		bool continueFlag = (line[line.length() - 1] != ';');

		_oss<<line;
		if (continueFlag) _oss<<"\r\n";

		return continueFlag;
	}

public:
	SQLFetcher(const std::string& prompt): CommandLineFetcher(prompt) {}
	std::string getSQL() { return _oss.str(); }
};

void addHashTableAction(DBDeployer& deployer, int paramsBeginIdx, const std::vector<std::string>& cmd)
{
	//add hash table <table_name> <table_count> [with hintId field <fiedl_name>] [to cluster <cluster_name>] in database <basic_database_name> <database_count>

	if (cmd.size() < 9 || cmd.size() > 16)
	{
		cout<<"Parameters error!"<<endl;
		return;
	}

	std::string tableName = cmd[paramsBeginIdx];
	int tableCount = atoi(cmd[paramsBeginIdx + 1].c_str());

	std::string hintIdField;
	std::string cluster;
	std::string database;
	int databaseCount = 0;

	paramsBeginIdx = 5;
	if (cmd[5] == "with" && cmd[6] == "hintId" && cmd[7] == "field")
	{
		hintIdField = cmd[8];
		paramsBeginIdx = 9;
	}

	if (cmd[paramsBeginIdx] == "to" && cmd[paramsBeginIdx + 1] == "cluster")
	{
		cluster = cmd[paramsBeginIdx + 2];
		paramsBeginIdx += 3;
	}

	if (cmd[paramsBeginIdx] == "in" && cmd[paramsBeginIdx + 1] == "database")
	{
		database = cmd[paramsBeginIdx + 2];
		databaseCount = atoi(cmd[paramsBeginIdx + 3].c_str());
	}

	if (paramsBeginIdx + 4 != (int)cmd.size() || database.empty() || databaseCount == 0)
	{
		cout<<"Parameters error!"<<endl;
		return;
	}

	SQLFetcher fetcher(" -- ");
	cout<<"Please input sql for create table "<<tableName<<":"<<endl<<endl;
	fetcher.runLoop();
	std::string sql = fetcher.getSQL();
	cout<<endl;

	deployer.createHashTable(tableName, sql, hintIdField, tableCount, cluster, database, databaseCount);
}

bool fetchSQLFromFile(DBDeployer& deployer, const std::string& filePath, const std::string& tableName, std::string& sql)
{
	const char* createHeader = "CREATE TABLE ";
	size_t headerLen = strlen(createHeader);

	std::vector<std::string> lines;

	if (!FileSystemUtil::fetchFileContentInLines(filePath, lines, true, false))
	{
		cout<<"Read file "<<filePath<<" failed"<<endl;
		return false;
	}

	bool found = false;
	bool maybeHeader = true;
	std::ostringstream oss;
	for (size_t i = 0; i < lines.size(); i++)
	{
		std::string trimedLine = StringUtil::trim(lines[i]);
		if (trimedLine.empty())
			continue;

		if (found && !maybeHeader)
			oss<<lines[i];

		if (maybeHeader && strncasecmp(trimedLine.c_str(), createHeader, headerLen) == 0)
		{
			size_t pos = trimedLine.find(tableName);
			if (pos != std::string::npos)
			{
				found = true;
				oss<<lines[i];
			}		
		}

		maybeHeader = (trimedLine[trimedLine.length() - 1] == ';');
		if (found && !maybeHeader)
			oss<<"\r\n";

		if (found && maybeHeader)
		{
			sql = oss.str();
			found = deployer.checkTableNameInCreateSQL(tableName, sql, true);
			if (found)
				return true;
		}
	}

	return false;
}

void addHashTableFromAction(DBDeployer& deployer, int paramsBeginIdx, const std::vector<std::string>& cmd)
{
	//add hash table from <sql_file_path> <table_name> <table_count> [with hintId field <fiedl_name>] [to cluster <cluster_name>] in database <basic_database_name> <database_count>

	if (cmd.size() < 11 || cmd.size() > 18)
	{
		cout<<"Parameters error!"<<endl;
		return;
	}

	std::string sqlFilePath = cmd[paramsBeginIdx];
	std::string tableName = cmd[paramsBeginIdx + 1];
	int tableCount = atoi(cmd[paramsBeginIdx + 2].c_str());

	std::string hintIdField;
	std::string cluster;
	std::string database;
	int databaseCount = 0;

	paramsBeginIdx = 7;
	if (cmd[7] == "with" && cmd[8] == "hintId" && cmd[9] == "field")
	{
		hintIdField = cmd[10];
		paramsBeginIdx = 11;
	}

	if (cmd[paramsBeginIdx] == "to" && cmd[paramsBeginIdx + 1] == "cluster")
	{
		cluster = cmd[paramsBeginIdx + 2];
		paramsBeginIdx += 3;
	}

	if (cmd[paramsBeginIdx] == "in" && cmd[paramsBeginIdx + 1] == "database")
	{
		database = cmd[paramsBeginIdx + 2];
		databaseCount = atoi(cmd[paramsBeginIdx + 3].c_str());
	}

	if (paramsBeginIdx + 4 != (int)cmd.size() || database.empty() || databaseCount == 0)
	{
		cout<<"Parameters error!"<<endl;
		return;
	}

	std::string sql;
	if (!fetchSQLFromFile(deployer, sqlFilePath, tableName, sql))
	{
		cout<<"Cannot fetch SQL for create table "<<tableName<<" in file "<<sqlFilePath<<endl;
		return;
	}

	deployer.createHashTable(tableName, sql, hintIdField, tableCount, cluster, database, databaseCount);
}

void initTableProcessor(CommandProcessor& processor, DBDeployer& deployer)
{
	processor.registerCmd("show all tables", [&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) {
		if (cmd.size() == 3)
			deployer.showAllTables("");
		else if (cmd.size() == 4)
			deployer.showAllTables(cmd[3]);
		else
			cout<<"Parameters error!"<<endl;
	});

	processor.registerCmd("show hash tables", [&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) {
		if (cmd.size() == 3)
			deployer.showHashTables("");
		else if (cmd.size() == 4)
			deployer.showHashTables(cmd[3]);
		else
			cout<<"Parameters error!"<<endl;
	});

	processor.registerCmd("show range tables", [&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) {
		if (cmd.size() == 3)
			deployer.showRangeTables("");
		else if (cmd.size() == 4)
			deployer.showRangeTables(cmd[3]);
		else
			cout<<"Parameters error!"<<endl;
	});

	processor.registerCmd("drop table", [&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) {
		if (cmd.size() == 3)
			deployer.dropTable(cmd[2], "");
		else if (cmd.size() == 4)
			deployer.dropTable(cmd[2], cmd[3]);
		else
		{
			cout<<"Parameters error!"<<endl;
			return;
		}
		cout<<"Drop completed."<<endl;
	});

	processor.registerCmd("add hash table", [&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) {
		addHashTableAction(deployer, paramsBeginIdx, cmd);
	});

	processor.registerCmd("add hash table from", [&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) {
		addHashTableFromAction(deployer, paramsBeginIdx, cmd);
	});
}

void initMiscProcessor(CommandProcessor& processor, DBDeployer& deployer)
{
	processor.registerCmd("update config time", [&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd) {
		if (cmd.size() != 3)
		{
			cout<<"Parameters error!"<<endl;
			return;
		}
		deployer.updateConfigTime();
	});

	{
		CmdExecutorPtr executor(new std::function<void (int paramsBeginIdx, const std::vector<std::string>& cmd)>(
			[&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd)
			{
				if (cmd.size() != 3)
				{
					cout<<"Parameters error!"<<endl;
					return;
				}
				deployer.confuseConfigDatabase();
			}
			));

		processor.registerCmd("confuse config db", executor);
		processor.registerCmd("confuse config database", executor);
	}

	{
		CmdExecutorPtr executor(new std::function<void (int paramsBeginIdx, const std::vector<std::string>& cmd)>(
			[&deployer](int paramsBeginIdx, const std::vector<std::string>& cmd)
			{
				if (cmd.size() != 3)
				{
					cout<<"Parameters error!"<<endl;
					return;
				}
				deployer.decodeConfigDatabase();
			}
			));

		processor.registerCmd("decode config db", executor);
		processor.registerCmd("decode config database", executor);
	}
}

void initCommandProcessor(CommandProcessor& processor, DBDeployer& deployer)
{
	initDatabaseOperations(processor, deployer);
	initInstanceOperations(processor, deployer);
	initDeployServerOperations(processor, deployer);
	initAccountOperations(processor, deployer);
	initTableProcessor(processor, deployer);
	initMiscProcessor(processor, deployer);
}

void commandLoop(DBDeployer& deployer)
{
	CommandProcessor processor;
	initCommandProcessor(processor, deployer);

	cout<<endl<<"Warnning: This deploy tool requires all administrator accounts of all databases are same."<<endl;
	cout<<endl<<"Warnning: If need to alter tables, please using DBProxy manager version."<<endl;
	cout<<endl<<"Enter 'help' to list all commands."<<endl<<endl;

	char *rawline;
	while ((rawline = linenoise("DBDeployer> ")) != NULL)
	{
		linenoiseHistoryAdd(rawline);
		linenoiseHistorySave(".dbdeployer.cmd.log");
		char *l = StringUtil::trim(rawline);
		std::string line(l);
		free(rawline);

		if (line.empty())
			continue;

		if (line == "exit" || line == "quit")
			break;

		if (line == "help")
		{
			showHelp();
			continue;
		}

		if (line[line.length() - 1] == ';')
			line.pop_back();

		if (!processor.execute(line))
			cout<<"Bad command."<<endl;
	}
}

void processScript(DBDeployer& deployer, const std::string& scriptFilepath)
{
	std::vector<std::string> lines;
	if (!FileSystemUtil::fetchFileContentInLines(scriptFilepath, lines, true, true))
	{
		cout<<"Read script file "<<scriptFilepath<<" failed."<<endl;
		return;
	}

	CommandProcessor processor;
	initCommandProcessor(processor, deployer);

	for (size_t i = 0; i < lines.size(); i++)
	{
		std::string line = lines[i];

		cout<<"-------------------------------------------------"<<endl;
		cout<<">>> "<<line<<endl<<endl;

		if (line[line.length() - 1] == ';')
			line.pop_back();

		if (!processor.execute(line))
			cout<<"Bad command."<<endl;

		cout<<endl<<endl;
	}
}

int main(int argc, const char* argv[])
{
	if (argc == 5 || argc == 6)
	{
		const char* host = argv[1];
		int port = atoi(argv[2]);
		const char* admin_user = argv[3];
		const char* admin_pwd = argv[4];

		ignoreSignals();

		MySQLClient::MySQLClientInit();

		DBDeployer deployer(host, port, admin_user, admin_pwd);
		if (argc == 5)
			commandLoop(deployer);
		else
			processScript(deployer, argv[5]);

		MySQLClient::MySQLClientEnd();
	}
	else
	{
		std::cout<<"Usage: "<<std::endl;
		std::cout<<"\t"<<argv[0]<<" config_db_host config_db_port admin_user admin_pwd"<<std::endl;
		std::cout<<"\t"<<argv[0]<<" config_db_host config_db_port admin_user admin_pwd scriptPath"<<std::endl;
	}
	
	return 0;
}
