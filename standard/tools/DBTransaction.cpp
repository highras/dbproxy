#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
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
	std::vector<int64_t> hints;
	std::vector<std::string> tablenames;
	std::vector<std::string> sqls;
};

void transaction(const Param& param)
{
	FPQWriter qw(3, "transaction");
	qw.param("hintIds", param.hints);
	qw.param("tableNames", param.tablenames);
	qw.param("sqls", param.sqls);
	
	struct timeval start, finish;
	std::shared_ptr<TCPClient> client = TCPClient::createClient(param.host, param.port);

	gettimeofday(&start, NULL);
	FPAnswerPtr answer = client->sendQuest(qw.take());
	gettimeofday(&finish, NULL);
	
	FPAReader ar(answer);
	if (ar.status())
	{
		std::cout<<"Query error!"<<std::endl;
		std::cout<<"Error code: "<<ar.wantInt("code")<<std::endl;
		std::cout<<"Expection: "<<ar.wantString("ex")<<std::endl;
	}
	else
	{
		std::cout<<"Query finished."<<std::endl<<"Transcation achieved."<<std::endl<<std::endl;
	}

	std::cout<<std::endl;
	timeCalc(start, finish);
}

int main(int argc, const char* argv[])
{
	Param param;

	if (argc == 4)
	{
		param.host = "localhost";
		param.port = 12321;

		std::string params = argv[1];
		std::vector<std::string> vec;
		StringUtil::split(params, ",; ", vec);

		for (size_t i = 0; i < vec.size(); i++)
		{
			param.hints.push_back(atoll(vec[i].c_str()));
		}

		params = argv[2];
		StringUtil::split(params, ",; ", param.tablenames);

		params = argv[3];
		StringUtil::split(params, ";", param.sqls);
	}
	else if (argc == 6)
	{
		param.host = argv[1];
		param.port = atoi(argv[2]);
		
		std::string params = argv[3];
		std::vector<std::string> vec;
		StringUtil::split(params, ",; ", vec);

		for (size_t i = 0; i < vec.size(); i++)
		{
			param.hints.push_back(atoll(vec[i].c_str()));
		}

		params = argv[4];
		StringUtil::split(params, ",; ", param.tablenames);

		params = argv[5];
		StringUtil::split(params, ";", param.sqls);
	}
	else
	{
		std::cout<<"Usage: "<<std::endl;
		std::cout<<"\t"<<argv[0]<<" hints tablenames sqls"<<std::endl;
		std::cout<<"\t"<<argv[0]<<" host port hints table_names sqls"<<std::endl;
		std::cout<<"\n\tNotes: host default is localhost, and port default is 12321."<<std::endl;
		std::cout<<"\tSample: "<<argv[0]<<" 1,2,3 tablw_1,table_2,table_3 \"sql_1;sql_2;sql_3\""<<std::endl;
		return 0;
	}
	
	ignoreSignals();
	transaction(param);
	return 0;
}
