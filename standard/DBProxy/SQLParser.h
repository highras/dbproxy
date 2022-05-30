#ifndef SQL_PARSER_H
#define SQL_PARSER_H

#include <string>

class SQLParser
{
	static bool findNextWord(char*& str, std::string* word);
	static bool checkStatement(const char* sql, const char* operation, int len);
	static bool findTableNameAfterFrom(const char* sql, int offset, std::string* tableName);
	static bool findTableNameForAlertTable(const char* sql, int offset, std::string* tableName);
	static bool findTableNameForDesc(const char* sql, int offset, std::string* tableName);
	static bool findTableNameForDataModificationSQL(const char* sql, int offset, std::string* tableName);
	static bool findTableNameOfSelect(const char* sql, int offset, std::string* tableName);
	static bool addTableNameSuffixForSelect(std::string& sql, const std::string& tableName, const char* suffix);

public:
	static void init();
	static void extractSQL(std::string& sql);
	static bool addTableSuffix(std::string& sql, const std::string& tableName, const char* suffix);
	static bool pretreatSQL(const std::string& sql, bool& forceMasterTask, std::string* tableName = NULL);
	static bool pretreatSelectSQL(const std::string& sql, bool& forceMasterTask, std::string* tableName = NULL);
};

#endif
