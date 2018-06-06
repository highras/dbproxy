#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <set>
#include "ignoreSignals.h"
#include "TCPClient.h"
#include "FPWriter.h"
#include "FPReader.h"
#include "StringUtil.h"

using namespace fpnn;

void timeCalc(struct timeval start, struct timeval finish)
{
	int sec = finish.tv_sec - start.tv_sec;
	int usec;
	
	if (finish.tv_usec >= start.tv_usec)
		usec = finish.tv_usec - start.tv_usec;
	else
	{
		usec = 100 * 10000 + finish.tv_usec - start.tv_usec;
		sec -= 1;
	}
	
	std::cout<<"time cost "<< (sec * 1000 + usec / 1000) << "."<<(usec % 1000)<<" ms"<<std::endl;
}

struct Param
{
	std::string host;
	int port;
	int64_t hint;
	std::string tablename;
	std::string sql;
	std::vector<std::string> params;
	std::string cluster;

	bool multiHints;
	std::set<int64_t> intHints;
	std::set<std::string> stringHints;
};

void printSelectResult(const std::vector<std::string>& fields, const std::vector<std::vector<std::string>>& rows)
{
	std::vector<size_t> fieldLens(fields.size(), 0);

	for (size_t i = 0; i < fields.size(); ++i)
		if (fields[i].length() > fieldLens[i])
			fieldLens[i] = fields[i].length();

	for (size_t i = 0; i < rows.size(); ++i)
		for(size_t j = 0; j < rows[i].size(); ++j)
			if (rows[i][j].length() > fieldLens[j])
				fieldLens[j] = rows[i][j].length();

	//-- top
	std::cout<<"+";
	for (size_t i = 0; i < fieldLens.size(); i++)
		std::cout<<std::string(fieldLens[i] + 2, '-')<<'+';
	std::cout<<std::endl;

	//-- fiels
	std::cout<<"|";
	for(size_t i = 0; i < fields.size(); ++i)
	{
		std::cout<<' '<<fields[i];
		if (fields[i].length() < fieldLens[i])
			std::cout<<std::string(fieldLens[i] - fields[i].length(), ' ');

		std::cout<<" |";
	}
	std::cout<<std::endl;

	//-- separator
	std::cout<<"+";
	for (size_t i = 0; i < fieldLens.size(); i++)
		std::cout<<std::string(fieldLens[i] + 2, '=')<<'+';
	std::cout<<std::endl;

	//-- data
	for (size_t i = 0; i < rows.size(); ++i)
	{
		std::cout<<"|";
		for(size_t j = 0; j < rows[i].size(); ++j)
		{
			std::cout<<' '<<rows[i][j];
			if (rows[i][j].length() < fieldLens[j])
				std::cout<<std::string(fieldLens[j] - rows[i][j].length(), ' ');

			std::cout<<" |";
		}
		std::cout<<std::endl;
	}

	//-- tail line
	std::cout<<"+";
	for (size_t i = 0; i < fieldLens.size(); i++)
		std::cout<<std::string(fieldLens[i] + 2, '-')<<'+';
	std::cout<<std::endl;

	std::cout<<rows.size()<<" rows in results."<<std::endl;
}
void printModifiedResult(std::vector<std::vector<int64_t>>& results)
{
	std::vector<std::string> fields{"equivalent_table_Id", "affectedRows", "insertId"};
	std::vector<std::vector<std::string>> rows;
	for (std::vector<int64_t>& result: results)
	{
		std::vector<std::string> rec{ std::to_string(result[0]), std::to_string(result[1]), std::to_string(result[2]) };
		rows.push_back(rec);
	}

	printSelectResult(fields, rows);
}
void printFailedOrInvalidInfos(const Param& param, FPAReader& ar)
{
	if (param.multiHints)
	{
		if (param.intHints.size())
		{
			std::vector<int64_t> failedIds = ar.get("failedIds", std::vector<int64_t>()); 
			std::vector<int64_t> invalidIds = ar.get("invalidIds", std::vector<int64_t>());

			std::string failedIdsStr = StringUtil::join(failedIds, ", ");
			std::string invalidIdsStr = StringUtil::join(invalidIds, ", ");

			if (failedIdsStr.length() || invalidIdsStr.length())
				std::cout<<std::endl;

			if (failedIdsStr.length())
				std::cout<<"failedIds: ["<<failedIdsStr<<"]"<<std::endl;
			if (invalidIdsStr.length())
				std::cout<<"invalidIds: ["<<invalidIdsStr<<"]"<<std::endl;
		}
		else if (param.stringHints.size())
		{
			std::vector<std::string> failedIds = ar.get("failedIds", std::vector<std::string>()); 
			std::string failedIdsStr = StringUtil::join(failedIds, ", ");

			if (failedIdsStr.length())
				std::cout<<std::endl<<"failedIds: ["<<failedIdsStr<<"]"<<std::endl;
		}
		else
		{
			std::vector<int64_t> failedIds = ar.get("failedIds", std::vector<int64_t>());
			std::string failedIdsStr = StringUtil::join(failedIds, ", ");

			if (failedIdsStr.length())
				std::cout<<std::endl<<"failedIds: ["<<failedIdsStr<<"]"<<std::endl;
		}
	}
}

void query(const Param& param)
{
	int paramsCount = 2;

	if (!param.tablename.empty())
		paramsCount++;

	if (!param.cluster.empty())
		paramsCount++;

	if (!param.params.empty())
		paramsCount++;

	std::string method;
	if (param.multiHints == false)
		method = "query";
	else if (param.intHints.size())
		method = "iQuery";
	else
		method = "sQuery";

	FPQWriter qw(paramsCount, method);

	if (param.multiHints == false)
		qw.param("hintId", param.hint);
	else if (param.intHints.size())
		qw.param("hintIds", param.intHints);
	else
		qw.param("hintIds", param.stringHints);
	
	qw.param("sql", param.sql);

	if (!param.tablename.empty())
		qw.param("tableName", param.tablename);

	if (!param.cluster.empty())
		qw.param("cluster", param.cluster);

	if (!param.params.empty())
		qw.param("params", param.params);
	
	struct timeval start, finish;
	std::shared_ptr<TCPClient> client = TCPClient::createClient(param.host, param.port);

	gettimeofday(&start, NULL);
	FPAnswerPtr answer = client->sendQuest(qw.take());
	gettimeofday(&finish, NULL);
	
	FPAReader ar(answer);
	if (ar.status())
	{
		std::cout<<"Query error!"<<std::endl;
		std::cout<<"Raiser: "<<ar.wantString("raiser")<<std::endl;
		std::cout<<"Error code: "<<ar.wantInt("code")<<std::endl;
		std::cout<<"Expection: "<<ar.wantString("ex")<<std::endl;
	}
	else
	{
		std::cout<<"Query finished."<<std::endl<<std::endl;

		std::vector<std::string> fields = ar.get("fields", std::vector<std::string>());
		std::vector<std::vector<int64_t>> results = ar.get("results", std::vector<std::vector<int64_t>>());
		if (fields.size())
		{
			std::vector<std::vector<std::string>> rows = ar.get("rows", std::vector<std::vector<std::string>>());
			printSelectResult(fields, rows);
			printFailedOrInvalidInfos(param, ar);
		}
		else if (results.size())
		{
			printModifiedResult(results);
			printFailedOrInvalidInfos(param, ar);
		}
		else
		{
			std::cout<<"affectedRows: "<<ar.wantInt("affectedRows")<<std::endl;
			std::cout<<"insertId: "<<ar.wantInt("insertId")<<std::endl;
		}
	}

	std::cout<<std::endl;
	timeCalc(start, finish);
}

int main(int argc, const char* argv[])
{
	Param param;

	param.host = "localhost";
	param.port = 12321;
	param.multiHints = false;

	int index = 1;
	bool badParams = false;
	bool gotHintId = false;
	bool gotSql = false;

	if (argc < 3)
		badParams = true;
	
	while (index < argc)
	{
		if (argv[index][0] == '-')
		{
			if (index+1 >= argc)
			{
				badParams = true;
				break;
			}

			if (strcmp(argv[index], "-h") == 0)
			{
				param.host = argv[index+1];
			}
			else if (strcmp(argv[index], "-p") == 0)
			{
				param.port = atoi(argv[index+1]);
			}
			else if (strcmp(argv[index], "-t") == 0)
			{
				param.tablename = argv[index+1];
			}
			else if (strcmp(argv[index], "-c") == 0)
			{
				param.cluster = argv[index+1];
			}
			else if (strcmp(argv[index], "-timeout") == 0)
			{
				ClientEngine::setQuestTimeout(atoi(argv[index+1]));
			}
			else if (strcmp(argv[index], "-i") == 0)
			{
				std::vector<std::string> vec;
				StringUtil::split(argv[index+1], ", ", vec);

				for (size_t i = 0; i < vec.size(); i++)
					param.intHints.insert(atoll(vec[i].c_str()));

				gotHintId = true;
				param.multiHints = true;
			}
			else if (strcmp(argv[index], "-s") == 0)
			{
				std::vector<std::string> vec;
				StringUtil::split(argv[index+1], ", ", vec);

				for (size_t i = 0; i < vec.size(); i++)
					param.stringHints.insert(vec[i]);

				gotHintId = true;
				param.multiHints = true;
			}
			else
			{
				badParams = true;
				break;
			}

			index += 2;
			continue;
		}

		if (!gotHintId)
		{
			param.hint = atoll(argv[index]);
			gotHintId = true;
		}
		else if (!gotSql)
		{
			param.sql = argv[index];
			gotSql = true;
		}
		else
		{
			param.params.push_back(argv[index]);
		}

		index++;
	}

	if (badParams)
	{
		std::cout<<"Usage: "<<std::endl;
		std::cout<<"\t"<<argv[0]<<" [-h host] [-p port] [-t table_name] [-timeout timeout_seconds] [-c cluster] <hintId | -i int_hintIds | -s string_hintIds> sql [param1 param2 ...]"<<std::endl;
		std::cout<<"\n\tNotes: host default is localhost, and port default is 12321."<<std::endl;
		return 0;
	}
	
	ignoreSignals();
	query(param);
	return 0;
}
