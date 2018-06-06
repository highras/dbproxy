#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include "ignoreSignals.h"
#include "TCPClient.h"
#include "FPWriter.h"
#include "FPReader.h"

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
void query(const Param& param)
{
	FPQWriter qw((param.tablename.empty() ? 2 : 3), "query");
	qw.param("hintId", param.hint);
	qw.param("sql", param.sql);

	if (!param.tablename.empty())
		qw.param("tableName", param.tablename);
	
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
		if (fields.size())
		{
			std::vector<std::vector<std::string>> rows = ar.get("rows", std::vector<std::vector<std::string>>());
			printSelectResult(fields, rows);
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

	if (argc == 3)
	{
		param.host = "localhost";
		param.port = 12321;
		param.hint = atoi(argv[1]);
		param.sql = argv[2];
	}
	else if (argc == 4)
	{
		param.host = "localhost";
		param.port = 12321;
		param.hint = atoi(argv[1]);
		param.tablename = argv[2];
		param.sql = argv[3];
	}
	else if (argc == 5)
	{
		param.host = argv[1];
		param.port = atoi(argv[2]);
		param.hint = atoll(argv[3]);
		param.sql = argv[4];
	}
	else if (argc == 6)
	{
		param.host = argv[1];
		param.port = atoi(argv[2]);
		param.hint = atoll(argv[3]);
		param.tablename = argv[4];
		param.sql = argv[5];
	}
	else
	{
		std::cout<<"Usage: "<<std::endl;
		std::cout<<"\t"<<argv[0]<<" hint sql"<<std::endl;
		std::cout<<"\t"<<argv[0]<<" hint table_name sql"<<std::endl;
		std::cout<<"\t"<<argv[0]<<" host port hint sql"<<std::endl;
		std::cout<<"\t"<<argv[0]<<" host port hint table_name sql"<<std::endl;
		std::cout<<"\n\tNotes: host default is localhost, and port default is 12321."<<std::endl;
		return 0;
	}
	
	ignoreSignals();
	query(param);
	return 0;
}
