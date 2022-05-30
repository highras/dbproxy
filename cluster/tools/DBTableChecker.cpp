#include <iostream>
#include <set>
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

bool checkException(FPAReader &ar)
{
	if (ar.status())
	{
		std::cout<<"Query error!"<<std::endl;
		std::cout<<"Raiser: "<<ar.wantString("raiser")<<std::endl;
		std::cout<<"Error code: "<<ar.wantInt("code")<<std::endl;
		std::cout<<"Expection: "<<ar.wantString("ex")<<std::endl;

		return false;
	}
	return true;
}

bool Tableheck(std::shared_ptr<TCPClient> client, const std::string& tableName, const std::string& cluster, std::set<int64_t> &failedIds)
{	
	FPQWriter qw(4, "iQuery");
	qw.param("hintIds", std::set<int64_t>());
	qw.param("tableName", tableName);
	qw.param("cluster", cluster);
	qw.param("sql", std::string("select * from ").append(tableName).append(" limit 1"));

	std::cout<<"Query interface 'iQuery' ..."<<std::endl;
	FPAnswerPtr answer = client->sendQuest(qw.take());
	FPAReader ar(answer);
	if (checkException(ar))
	{
		failedIds = ar.get("failedIds", std::set<int64_t>());
		return true;
	}
	return false;
}

void check(const std::string& host, int port, const std::string& tableName, const std::string& cluster)
{
	struct timeval start, finish;
	std::shared_ptr<TCPClient> client = TCPClient::createClient(host, port);
	gettimeofday(&start, NULL);
	{
		FPQWriter qw(2, "splitInfo");
		qw.param("tableName", tableName);
		qw.param("cluster", cluster);

		std::cout<<"Query interface 'splitInfo' ..."<<std::endl;
		FPAnswerPtr answer = client->sendQuest(qw.take());
		FPAReader ar(answer);
		if (!checkException(ar))
			return;

		if (ar.wantBool("splitByRange"))
		{
			int rangeCount = ar.wantInt("count");
			int rangeSpan = ar.wantInt("span");
			std::string databaseCategory = ar.wantString("databaseCategory");
			std::cout<<"Split type: Range"<<std::endl;
			std::cout<<"Range Span: "<<rangeSpan<<std::endl;
			std::cout<<"Range Count: "<<rangeCount<<std::endl;
			std::cout<<"Database Category: "<<databaseCategory<<std::endl;
			std::cout<<"Split Hint: "<<ar.wantString("splitHint")<<std::endl;

			bool secondarySplit = ar.wantBool("secondarySplit");
			if (secondarySplit)
			{
				std::cout<<"SecondarySplit: Yes"<<std::endl;
				std::cout<<"Secondary Split Span: "<<ar.wantInt("secondarySplitSpan")<<std::endl;
			}
			else
				std::cout<<"SecondarySplit: No"<<std::endl;

			std::set<int> oddEvenRanges;
			{
				FPQWriter qw(1, "categoryInfo");
				qw.param("databaseCategory", databaseCategory);

				std::cout<<"Query interface 'categoryInfo' ..."<<std::endl;
				FPAnswerPtr answer = client->sendQuest(qw.take());
				FPAReader ar(answer);
				if (!checkException(ar))
					return;

				if (ar.wantInt("splitCount") != rangeCount)
				{
					std::cout<<"Error!\nInterface splitInfo return "<<rangeCount<<" ranges, but categoryInfo return "<<ar.wantInt("splitCount")<<" ranges.\n";
					return;
				}
				int oddEvenCount = ar.wantInt("oddEvenCount");
				if (oddEvenCount)
				{
					std::cout<<"Total Range Span count "<<(rangeCount + oddEvenCount)<<", include "<<oddEvenCount<<" odd-even ranges."<<std::endl;
					std::cout<<"Odd-even ranges are : ";
					oddEvenRanges = ar.want("oddEvenIndexes", std::set<int>());
					for (int id: oddEvenRanges)
						std::cout<<id<<" ";
					std::cout<<"."<<std::endl;
				}
			}

			std::cout<<"----------------------------------"<<std::endl;
			std::set<int64_t> failedIds;
			if (!Tableheck(client, tableName, cluster, failedIds))
				return;

			gettimeofday(&finish, NULL);
			std::cout<<"Check finished."<<std::endl;

			if (failedIds.size())
			{
				std::cout<<"Failed with "<<failedIds.size()<<" sub-tables."<<std::endl;
				for (int64_t hintId: failedIds)
				{
					int64_t rangeIdx = hintId/rangeSpan;
					std::cout<<"... sub-table hint id "<<hintId<<" failed."<<std::endl;
					if (oddEvenRanges.find(rangeIdx) != oddEvenRanges.end())
					{
						if (hintId & 0x1)
							std::cout<<"... ... odd database with range index "<<rangeIdx<<".\n";
						else
							std::cout<<"... ... even database with range index "<<rangeIdx<<".\n";
					}
					else
					{
						std::cout<<"... ... range index "<<rangeIdx<<".\n";
					}

					if (secondarySplit)
					{
						int64_t subIdx = hintId - rangeIdx * rangeSpan;
						subIdx /= ar.wantInt("seconddarySplitSpan");
						std::cout<<"... ... secondary split index "<<subIdx<<".\n";
					}
				}
			}
			else
				std::cout<<"All sub-tables are OK!"<<std::endl;
		}
		else
		{
			int tableCount = ar.wantInt("tableCount");
			std::cout<<"Split type: Hash"<<std::endl;
			std::cout<<"Table Count: "<<tableCount<<std::endl;
			std::cout<<"Split Hint: "<<ar.wantString("splitHint")<<std::endl;

			std::cout<<"----------------------------------"<<std::endl;
			std::set<int64_t> failedIds;
			if (!Tableheck(client, tableName, cluster, failedIds))
				return;

			gettimeofday(&finish, NULL);
			std::cout<<"Check finished."<<std::endl;

			if (failedIds.size())
			{
				std::cout<<"Failed with "<<failedIds.size()<<" sub-tables."<<std::endl;
				for (int64_t hintId: failedIds)
					std::cout<<"... sub-table hint id "<<hintId<<" failed."<<std::endl;
			}
			else
				std::cout<<"All sub-tables are OK!"<<std::endl;
		}
		timeCalc(start, finish);
	}
}

int main(int argc, const char* argv[])
{
	if (argc == 5)
	{
		const char* host = argv[1];
		int port = atoi(argv[2]);
		const char* tablename = argv[3];
		const char* cluster = argv[4];

		ignoreSignals();
		check(host, port, tablename, cluster);
	}
	else
	{
		std::cout<<"Usage: "<<std::endl;
		std::cout<<"\t"<<argv[0]<<" DBProxy_host DBProxy_port table_name cluster"<<std::endl;
	}
	
	return 0;
}
