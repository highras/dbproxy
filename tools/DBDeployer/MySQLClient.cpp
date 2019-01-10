#include <iostream>
//#include "FPLog.h"
#include "DataRouterErrorInfo.h"
#include "MySQLClient.h"

using namespace fpnn;

std::mutex MySQLClient::_mutex;
std::string MySQLClient::_default_connection_charset("utf8");

void MySQLClient::MySQLClientInit()
{
	mysql_library_init(0, NULL, NULL);
}

void MySQLClient::MySQLClientEnd()
{
	mysql_library_end();
}

void MySQLClient::setDefaultConnectionCharacterSetName(const std::string& connCharacterSetName)
{
	_default_connection_charset = connCharacterSetName;
	//LOG_INFO("Connection character set name: %s", _default_connection_charset.c_str());
}

MySQLClient::MySQLClient(const std::string &host, int port, const std::string &username, const std::string &password, const std::string &database, int timeout_seconds)
	: _client(0), _host(host), _port(port), _username(username), _password(password), _database(database), _timeout_seconds(timeout_seconds), _lastOperated(0)
{	
	//mysql_thread_init();
	connect();
}

MySQLClient::~MySQLClient()
{
	cleanup();
	mysql_thread_end();
}

bool MySQLClient::connect(const char *connection_charset_name)
{
	if (_client)
		return true;
		
	_client = mysql_init(NULL);
	if (!_client)
		return false;

	if (!connection_charset_name)
		connection_charset_name = _default_connection_charset.c_str();
		
	my_bool reconnect = 1;
	mysql_options(_client, MYSQL_OPT_RECONNECT, &reconnect);
	mysql_options(_client, MYSQL_SET_CHARSET_NAME, connection_charset_name);

	if (_timeout_seconds)
	{
		unsigned int timeOut = (unsigned int)_timeout_seconds;
		mysql_options(_client, MYSQL_OPT_CONNECT_TIMEOUT, &timeOut);
		mysql_options(_client, MYSQL_OPT_READ_TIMEOUT, &timeOut);
		mysql_options(_client, MYSQL_OPT_WRITE_TIMEOUT, &timeOut);
	}

	_mutex.lock();
	MYSQL *retClient = mysql_real_connect(_client, _host.c_str(), _username.c_str(), _password.c_str(), _database.length() ? _database.c_str() : NULL, _port, NULL, 0);
	_mutex.unlock();
	
	if (!retClient)
	{
		cleanup();
		return false;
	}
	
	mysql_set_character_set(_client, connection_charset_name);
	time(&_lastOperated);
	return true;
}

void MySQLClient::cleanup()
{
	if (_client)
	{
		mysql_close(_client);
		_client = NULL;
	}
}

bool MySQLClient::ping()
{
	if ( mysql_ping(_client) == 0)
	{
		time(&_lastOperated);
		return true;
	}
	cleanCheck(mysql_errno(_client));
	return false;
}

void MySQLClient::escapeStrings(std::vector<std::string>& strings)
{
	if (strings.size() == 0)
		return;

	std::vector<std::string> results;
	results.reserve(strings.size());

	for (size_t i = 0; i < strings.size(); i++)
	{
		char *buffer = (char *)malloc(strings[i].length() * 2 + 1);
		mysql_real_escape_string(_client, buffer, strings[i].c_str(), strings[i].length());
		results.push_back(buffer);
		free(buffer);
	}

	strings.swap(results);
}

FPAnswerPtr MySQLClient::generateExceptionAnswer(const FPQuestPtr quest)
{
	return ErrorInfo::MySQLExceptionAnswer(quest, mysql_errno(_client), mysql_error(_client), mysql_sqlstate(_client));
}

FPAnswerPtr MySQLClient::generateExceptionAnswer(const FPQuestPtr quest, int index, const std::string& sql)
{
	std::string ex;

	ex.append("Excepted index: ").append(std::to_string(index));
	ex.append(" Excepted SQL: ").append(sql);
	ex.append(". [MySQL Exception] errno: ");
	ex.append(std::to_string(mysql_errno(_client))).append(", error: '");
	ex.append(mysql_error(_client)).append("', sql status: '");
	ex.append(mysql_sqlstate(_client)).append("'");

	return FPAWriter::errorAnswer(quest, ErrorInfo::MySQLExceptionCode, ex, ErrorInfo::raiser_MySQL);
}

bool MySQLClient::adjustCurrentDatabase(const std::string& database)
{
	if (database.empty())
	{
		_database.clear();
		return true;
	}

	if (_database == database)
		return true;
		
	if (!mysql_select_db(_client, database.c_str()))
	{
		_database = database;
		return true;
	}
	else
		return false;
}

class MySQLResultGuard
{
	MYSQL_RES *_res;
public:
	MySQLResultGuard(MYSQL_RES *res): _res(res) {  }
	~MySQLResultGuard() { mysql_free_result(_res); }
};

FPAnswerPtr MySQLClient::query(const std::string& database, const std::string& sql, const FPQuestPtr quest)
{
	if (!adjustCurrentDatabase(database))
	{
		return generateExceptionAnswer(quest);
	}
	
	if (mysql_real_query(_client, sql.data(), sql.length()))
	{
		return generateExceptionAnswer(quest);
	}
	
	time(&_lastOperated);
	MYSQL_RES *res = mysql_store_result(_client);
	if (!res)
	{
		if (mysql_errno(_client))
		{
			return generateExceptionAnswer(quest);
		}
		else if (mysql_field_count(_client) == 0)
		{
			FPAWriter aw(2, quest);
			aw.param("affectedRows", mysql_affected_rows(_client));
			aw.param("insertId", mysql_insert_id(_client));
			return aw.take();
		}
		else
		{
			return generateExceptionAnswer(quest);
		}
	}

	MySQLResultGuard mrg(res);

	FPAWriter aw(2, quest);
	int num_fields = mysql_num_fields(res);
	MYSQL_FIELD *fields =  mysql_fetch_fields(res);

	aw.paramArray("fields", num_fields);
	for(int i = 0; i < num_fields; i++)
		aw.param(fields[i].name);

	int num_rows = mysql_num_rows(res);
	aw.paramArray("rows", num_rows);

	MYSQL_ROW row;
	while ((row = mysql_fetch_row(res))) 
	{
		aw.paramArray(num_fields);
		unsigned long *lengths = mysql_fetch_lengths(res);
		
		for(int i = 0; i < num_fields; i++) 
		{
			std::string data(row[i], lengths[i]);
			aw.param(data);
		}
	}

	return aw.take();
}

bool MySQLClient::query(const std::string& database, const std::string& sql, QueryResult &result)
{
	result.type = QueryResult::ErrorType;

	if (!adjustCurrentDatabase(database))
	{
		//LOG_ERROR("Exception: mysql_errno: %d, mysql_error: %s, mysql_sqlstate: %s", mysql_errno(_client), mysql_error(_client), mysql_sqlstate(_client));
		std::cout<<"Exception: mysql_errno: "<<mysql_errno(_client)<<", mysql_error: "<<mysql_error(_client)<<", mysql_sqlstate: "<<mysql_sqlstate(_client)<<std::endl;
		return false;
	}

	if (mysql_real_query(_client, sql.data(), sql.length()))
	{
		//LOG_ERROR("Exception: mysql_errno: %d, mysql_error: %s, mysql_sqlstate: %s", mysql_errno(_client), mysql_error(_client), mysql_sqlstate(_client));
		std::cout<<"Exception: mysql_errno: "<<mysql_errno(_client)<<", mysql_error: "<<mysql_error(_client)<<", mysql_sqlstate: "<<mysql_sqlstate(_client)<<std::endl;
		std::cout<<"[database] "<<database<<std::endl;
		std::cout<<"[sql] "<<sql<<std::endl;
		return false;
	}
	
	time(&_lastOperated);
	MYSQL_RES *res = mysql_store_result(_client);
	if (!res)
	{
		if (mysql_errno(_client))
		{
			//LOG_ERROR("Exception: mysql_errno: %d, mysql_error: %s, mysql_sqlstate: %s", mysql_errno(_client), mysql_error(_client), mysql_sqlstate(_client));
			std::cout<<"Exception: mysql_errno: "<<mysql_errno(_client)<<", mysql_error: "<<mysql_error(_client)<<", mysql_sqlstate: "<<mysql_sqlstate(_client)<<std::endl;
			return false;
		}
		else if (mysql_field_count(_client) == 0)
		{
			result.type = QueryResult::ModifyType;
			result.affectedRows = mysql_affected_rows(_client);
			result.insertId = mysql_insert_id(_client);

			return true;
		}
		else
		{
			//LOG_ERROR("Exception: mysql_errno: %d, mysql_error: %s, mysql_sqlstate: %s", mysql_errno(_client), mysql_error(_client), mysql_sqlstate(_client));
			std::cout<<"Exception: mysql_errno: "<<mysql_errno(_client)<<", mysql_error: "<<mysql_error(_client)<<", mysql_sqlstate: "<<mysql_sqlstate(_client)<<std::endl;
			std::cout<<"[database] "<<database<<std::endl;
			std::cout<<"[sql] "<<sql<<std::endl;
			return false;
		}
	}
	
	int num_fields = mysql_num_fields(res);
	{
		MYSQL_FIELD *fields =  mysql_fetch_fields(res);
		for(int i = 0; i < num_fields; i++)
			result.fields.push_back(fields[i].name);
	}
	
	MYSQL_ROW row;
	result.type = QueryResult::SelectType;
	while ((row = mysql_fetch_row(res))) 
	{
		std::vector<std::string> rowData;
		unsigned long *lengths = mysql_fetch_lengths(res);
		
		for(int i = 0; i < num_fields; i++) 
		{
			std::string data(row[i], lengths[i]);
			rowData.push_back(data);
		}
		result.rows.push_back(rowData);
	}
	mysql_free_result(res);
	
	return true;
}

//-- If error occurred, return error answer. If seccussed, return nullptr.
FPAnswerPtr MySQLClient::executeTranscationStatement(const std::string& sql, const FPQuestPtr quest, int index)
{
	if (mysql_real_query(_client, sql.data(), sql.length()))
	{
		mysql_rollback(_client);
		return generateExceptionAnswer(quest, index, sql);
	}

	MYSQL_RES *res = mysql_store_result(_client);
	if (!res)
	{
		if (mysql_errno(_client))
		{
			mysql_rollback(_client);
			return generateExceptionAnswer(quest, index, sql);
		}
		return nullptr;
	}

	mysql_free_result(res);
	return nullptr;
}

FPAnswerPtr MySQLClient::transaction(const std::string& database, const std::vector<std::string>& sqls, const FPQuestPtr quest)
{
	if (!adjustCurrentDatabase(database))
	{
		return generateExceptionAnswer(quest);
	}

	FPAnswerPtr answer = executeTranscationStatement("START TRANSACTION", quest, -1);
	if (answer)
		return answer;

	for (size_t i = 0; i < sqls.size(); i++)
	{
		answer = executeTranscationStatement(sqls[i], quest, i);
		if (answer)
			return answer;
	}

	if (mysql_commit(_client))
	{
		mysql_rollback(_client);
		return generateExceptionAnswer(quest, sqls.size(), "COMMIT");
	}

	time(&_lastOperated);

	return FPAWriter::emptyAnswer(quest);
}
