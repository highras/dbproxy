#include <strings.h>
#include "StringUtil.h"
#include "SQLParser.h"

static const uint8_t SQL_extractHead = 0x1;
static const uint8_t SQL_extractTail = 0x2;

static const uint8_t SQL_charBeforeNextWord = 0x4;
static const uint8_t SQL_charAfterNextWord = 0x8;

static fpnn::StringUtil::CharMarkMap<uint8_t> _charMarkMap;

void SQLParser::init()
{
	_charMarkMap.init(" \t\n\r", SQL_extractHead);
	_charMarkMap.init(" \t\n\r;\0", 6, SQL_extractTail);
	_charMarkMap.init(" \t,*()\n\r", SQL_charBeforeNextWord);
	_charMarkMap.init(" \t,*()\n\r\0", 9, SQL_charAfterNextWord);
}

void SQLParser::extractSQL(std::string& sql)
{
	const char* header = sql.data();
	int queryLength = (int)sql.length();

	while (queryLength)
	{
		char c = header[queryLength - 1];
		if (_charMarkMap.check(c, SQL_extractTail))
		//if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';' || c == 0)
			queryLength -= 1;
		else
			break;
	}

	int headOffset = 0;
	while (queryLength && _charMarkMap.check(*header, SQL_extractHead))
	//while (queryLength && (*header == ' ' || *header == '\t' || *header == '\n' || *header == '\r'))
	{
		header += 1;
		headOffset += 1;
		queryLength -= 1;
	}

	if (queryLength < (int)sql.length())
	{
		std::string s = sql.substr(headOffset, queryLength);
		sql.swap(s);
	}
}

bool SQLParser::checkStatement(const char* sql, const char* operation, int len)
{
	if (strncasecmp(sql, operation, len))
		return false;

	if (isalnum(sql[len]))
		return false;

	if (iscntrl(sql[len]))
		return false;

	if (sql[len])
		return true;
	else
		return false;
}

bool SQLParser::findNextWord(char*& s, std::string* word)
{

	while (*s)
	{
		if (_charMarkMap.check(*s, SQL_charBeforeNextWord))
		//if (*s == ' ' || *s == '\t' || *s == ',' || *s == '*' || *s == '(' || *s == ')' || *s == '\r' || *s == '\n')
			s += 1;
		else
			break;
	}

	char* head = s;
	int len = 0;

	while (*s)
	{
		if (isalnum(*s)){
				//Do s += 1 & len += 1
		}
		else if (_charMarkMap.check(*s, SQL_charAfterNextWord))
		//else if (*s == ' ' || *s == '\t' || *s == ',' || *s == '*' || *s == '(' || *s == ')' || *s == '\r' || *s == '\n' || *s == 0)
			break;

		s += 1;
		len += 1;
	}

	if (len)
	{
		word->assign(head, len);
		return true;
	}
	return false;
}

bool SQLParser::addTableNameSuffixForSelect(std::string& sql, const std::string& tableName, const char* suffix)
{
	const int basicOffset = 7; 	//-- 'select' is 6 characters, and a separator follow 'select'.

	char* sqlHeader = const_cast<char*>(sql.c_str()); 
	char* s = sqlHeader + basicOffset;

	bool found = false;

	while (*s){
		while (*s)
		{
			if (isalnum(*s))
				break;
			else if (_charMarkMap.check(*s, SQL_charBeforeNextWord))
				s += 1;
			else
				break;
		}

		char* head = s;
		int len = 0;

		while (*s)
		{
			if (isalnum(*s)){
				//Do s += 1 & len += 1
			}
			else if (_charMarkMap.check(*s, SQL_charAfterNextWord))
			//else if (*s == ' ' || *s == '\t' || *s == ',' || *s == '*' || *s == '(' || *s == ')' || *s == '\r' || *s == '\n' || *s == 0 )
				break;

			s += 1;
			len += 1;
		}

		if (found){

			if(len > 0){
				std::string sqlTableName(head, len);
				if (sqlTableName == tableName)
				{
					size_t pos = (size_t)(head - sqlHeader);
					sql.insert(pos + len, suffix);
					return true;
				}
			}
			return false;
		}

		if (len == 4)
		{
			if (!strncasecmp(head, "from", len))
				found = true;
		}
		else if(!len) return false;
	}
	return false;
}

bool SQLParser::findTableNameOfSelect(const char* sql, int offset, std::string* tableName)
{
	char* s = const_cast<char*>(sql);
	s += offset;

	bool found = false;

	while (*s){
		while (*s)
		{
			if (isalnum(*s))
				break;
			else if (_charMarkMap.check(*s, SQL_charBeforeNextWord))
			//else if (*s == ' ' || *s == '\t' || *s == ',' || *s == '*' || *s == '(' || *s == ')' || *s == '\r' || *s == '\n')
				s += 1;
			else
				break;
		}

		char* head = s;
		int len = 0;

		while (*s)
		{
			if (isalnum(*s)){
				//Do s += 1 & len += 1
			}
			else if (_charMarkMap.check(*s, SQL_charAfterNextWord))
			//else if (*s == ' ' || *s == '\t' || *s == ',' || *s == '*' || *s == '(' || *s == ')' || *s == '\r' || *s == '\n' || *s == 0 )
				break;

			s += 1;
			len += 1;
		}

		if (found){

			if(len > 0){
				tableName->assign(head, len);
				return true;
			}
			else return false;
		}

		if (len == 4)
		{
			if (!strncasecmp(head, "from", len))
				found = true;
		}
		else if(!len) return false;
	}
	return false;
}

bool SQLParser::findTableNameAfterFrom(const char* sql, int offset, std::string* tableName)
{
	char* s = const_cast<char*>(sql);
	s += offset;

	while (findNextWord(s, tableName))
	{
		if (tableName->length() == 4 && strcasecmp(tableName->c_str(), "from") == 0)
			return findNextWord(s, tableName);
	}
	
	return false;
}

bool SQLParser::findTableNameForAlertTable(const char* sql, int offset, std::string* tableName)
{
	char* s = const_cast<char*>(sql);
	s += offset;

	while (findNextWord(s, tableName))
	{
		if (tableName->length() == 5 && strcasecmp(tableName->c_str(), "TABLE") == 0)
			return findNextWord(s, tableName);
	}
	
	return false;
}

bool SQLParser::findTableNameForDesc(const char* sql, int offset, std::string* tableName)
{
	char* s = const_cast<char*>(sql);
	s += offset;

	return findNextWord(s, tableName);
}

bool SQLParser::findTableNameForDataModificationSQL(const char* sql, int offset, std::string* tableName)
{
	char* s = const_cast<char*>(sql);
	s += offset;

	while (findNextWord(s, tableName))
	{
		switch (tableName->length())
		{
			case 0:
				return false;

			case 4:
				if (strcasecmp(tableName->c_str(), "INTO"))
					return true;
				else
					break;

			case 6:
				if (strcasecmp(tableName->c_str(), "IGNORE"))
					return true;
				else
					break;

			case 7:
				if (strcasecmp(tableName->c_str(), "DELAYED"))
					return true;
				else
					break;

			case 12:
				if (strcasecmp(tableName->c_str(), "LOW_PRIORITY"))
					return true;
				else
					break;

			case 13:
				if (strcasecmp(tableName->c_str(), "HIGH_PRIORITY"))
					return true;
				else
					break;

			default:
				return true;
		}
	}
	return false;
}

bool SQLParser::pretreatSelectSQL(const std::string& sql, bool& forceMasterTask, std::string* tableName)
{
#ifdef DBProxy_Manager_Version
	return pretreatSQL(sql, forceMasterTask, tableName);
#else
	if (checkStatement(sql.c_str(), "select", 6))
	{
		forceMasterTask = false;
		//return tableName ? findTableNameAfterFrom(sql.c_str(), 7, tableName) : true;
		return tableName ? findTableNameOfSelect(sql.c_str(), 7, tableName) : true;
	}
	return false;
#endif
}

bool SQLParser::pretreatSQL(const std::string& sql, bool& forceMasterTask, std::string* tableName)
{
	if (checkStatement(sql.c_str(), "select", 6))
	{
		forceMasterTask = false;
		//return tableName ? findTableNameAfterFrom(sql.c_str(), 7, tableName) : true;
		return tableName ? findTableNameOfSelect(sql.c_str(), 7, tableName) : true;
	}
	
	if (checkStatement(sql.c_str(), "update", 6))
	{
		forceMasterTask = true;
		return tableName ? findTableNameForDataModificationSQL(sql.c_str(), 7, tableName) : true;
	}
	
	if (checkStatement(sql.c_str(), "insert", 6))
	{
		forceMasterTask = true;
		return tableName ? findTableNameForDataModificationSQL(sql.c_str(), 7, tableName) : true;
	}
	
	if (checkStatement(sql.c_str(), "replace", 7))
	{
		forceMasterTask = true;
		return tableName ? findTableNameForDataModificationSQL(sql.c_str(), 8, tableName) : true;
	}
	
	if (checkStatement(sql.c_str(), "delete", 6))
	{
		forceMasterTask = true;
		return tableName ? findTableNameAfterFrom(sql.c_str(), 7, tableName) : true;
	}
	
	if (checkStatement(sql.c_str(), "desc", 4))
	{
		forceMasterTask = false;
		return tableName ? findTableNameForDesc(sql.c_str(), 5, tableName) : true;
	}
	
	if (checkStatement(sql.c_str(), "describe", 8))
	{
		forceMasterTask = false;
		return tableName ? findTableNameForDesc(sql.c_str(), 9, tableName) : true;
	}

	if (checkStatement(sql.c_str(), "explain", 7))
	{
		forceMasterTask = false;
		return tableName ? findTableNameForDesc(sql.c_str(), 8, tableName) : true;
	}

#ifdef DBProxy_Manager_Version
	if (checkStatement(sql.c_str(), "show create table", 17))
	{
		forceMasterTask = false;
		return tableName ? findTableNameForDesc(sql.c_str(), 18, tableName) : true;
	}
	
	if (checkStatement(sql.c_str(), "alter", 5))
	{
		forceMasterTask = true;
		return tableName ? findTableNameForAlertTable(sql.c_str(), 6, tableName) : true;
	}
#endif

	return false;
}

bool SQLParser::addTableSuffix(std::string& sql, const std::string& tableName, const char* suffix)
{
	if (suffix)
	{
		if (checkStatement(sql.c_str(), "select", 6))
			return addTableNameSuffixForSelect(sql, tableName, suffix);

		size_t found = sql.find(tableName);
		if (found != std::string::npos)
		{
			sql.insert(found + tableName.length(), suffix);
		}
		else
			return false;
	}

	return true;
}
