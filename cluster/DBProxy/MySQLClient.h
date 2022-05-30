#ifndef MySQL_Client_h_
#define MySQL_Client_h_

#include <mutex>
#include <string>
#include <sstream>
#include <vector>
#include <mysql.h>
#include <errmsg.h>
#include "FPWriter.h"

using fpnn::FPAnswerPtr;
using fpnn::FPQuestPtr;

struct QueryResult
{
	enum ResultType
	{
		SelectType,
		ModifyType,
		ErrorType
	};

	enum ResultType type;
	std::vector<std::string> fields;
	std::vector<std::vector<std::string>> rows;
	int affectedRows;
	int64_t insertId;

	QueryResult(): type(ErrorType), affectedRows(0), insertId(0) {}
};
typedef std::shared_ptr<QueryResult> QueryResultPtr;

class MySQLClient
{
	MYSQL *_client;
	
	std::string _host;
	int _port;
	std::string _username;
	std::string _password;
	std::string _database;
	int _timeout_seconds;
	
	time_t _lastOperated;

	static std::mutex _mutex;
	static std::string _default_connection_charset;
	
private:
	FPAnswerPtr generateExceptionAnswer(const FPQuestPtr quest);
	FPAnswerPtr generateExceptionAnswer(const FPQuestPtr quest, int index, const std::string& sql);
	
	inline void cleanCheck(unsigned int mySQLErrno)
	{
		if (mySQLErrno == CR_SERVER_GONE_ERROR || mySQLErrno == CR_SERVER_LOST)
			cleanup();
	}
	bool adjustCurrentDatabase(const std::string& database);
	//-- If error occurred, return error answer. If seccussed, return nullptr.
	FPAnswerPtr executeTranscationStatement(const std::string& sql, const FPQuestPtr quest, int index);
	
public:
	static void MySQLClientInit();
	static void MySQLClientEnd();
	static void setDefaultConnectionCharacterSetName(const std::string& connCharacterSetName);
	
	MySQLClient(const std::string &host, int port, const std::string &username, const std::string &password, const std::string &database = std::string(), int timeout_seconds = 0);
	~MySQLClient();
	
	bool connect(const char *connection_charset_name = NULL);
	void cleanup();
	
	inline time_t lastOperatedTime() { return _lastOperated; }
	inline bool connected() { return (_client != NULL); }
	bool ping();
	void escapeStrings(std::vector<std::string>& strings);

	FPAnswerPtr query(const std::string& database, const std::string& sql, const FPQuestPtr quest);
	bool query(const std::string& database, const std::string& sql, QueryResult &result);
	FPAnswerPtr transaction(const std::string& database, const std::vector<std::string>& sqls, const FPQuestPtr quest);
};

#endif
