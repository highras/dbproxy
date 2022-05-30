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

bool checkException(FPAReader &ar, bool openTail = false)
{
	if (ar.status())
	{
		if (openTail)
			std::cout<<"[Error]"<<std::endl;

		std::cout<<"Query error!"<<std::endl;
		std::cout<<"Error code: "<<ar.wantInt("code")<<std::endl;
		std::cout<<"Expection: "<<ar.wantString("ex")<<std::endl;

		return false;
	}
	return true;
}

std::set<int64_t> getHintIds(std::shared_ptr<TCPClient> client, const std::string& tableName)
{
	std::cout<<"Query interface 'allSplitHintIds' ..."<<std::endl;

	FPQWriter qw(1, "allSplitHintIds");
	qw.param("tableName", tableName);

	std::set<int64_t> hintIds;
	FPAnswerPtr answer = client->sendQuest(qw.take());
	FPAReader ar(answer);
	if (checkException(ar))
		hintIds = ar.want("hintIds", std::set<int64_t>());
	
	return hintIds;
}

void printErrorInfos(std::vector<std::string> &errorInfos, std::set<int64_t> &hintIds)
{
	std::cout<<"----------------------------------"<<std::endl;
	if (errorInfos.empty() && hintIds.empty())
	{
		std::cout<<"All sub-tables are OK!"<<std::endl;
		return;
	}
	if (!errorInfos.empty())
	{
		std::cout<<std::endl<<"Error Infos ("<<errorInfos.size()<<"):"<<std::endl;
		for (size_t i = 0; i < errorInfos.size(); i++)
			std::cout<<errorInfos[i]<<std::endl;
	}
	if (!hintIds.empty())
	{
		std::cout<<std::endl<<"Remained "<<hintIds.size()<<" hintIds returned by interface 'allSplitHintIds'."<<std::endl;
		for (int64_t id: hintIds)
			std::cout<<"    "<<id<<std::endl;

		std::cout<<std::endl;
	}
}

void checkTable(std::shared_ptr<TCPClient> client, int64_t hintId, const std::string &tableName,
	const std::string &sql, int &standardTableIdx, std::string indexInfo, std::set<int64_t> &hintIds,
	std::vector<std::vector<std::string>> &columsDefinitions, std::vector<std::string> &errorInfos)
{
	std::cout<<"... checking sub-table "<<indexInfo<<" by hintId: "<<hintId<<" ... ";
	bool openTail = true;

	FPQWriter qw(3, "query");
	qw.param("hintId", hintId);
	qw.param("tableName", tableName);
	qw.param("sql", sql);

	FPAnswerPtr answer = client->sendQuest(qw.take());
	FPAReader ar(answer);
	if (!checkException(ar, openTail))
	{
		std::string errorInfo("Check sub-table ");
		errorInfo.append(indexInfo).append(" by hintId: ");
		errorInfo.append(std::to_string(hintId)).append(" failed.");
		errorInfos.push_back(errorInfo);
		std::cout<<errorInfo<<std::endl;
		return;
	}

	if (hintIds.find(hintId) != hintIds.end())
		hintIds.erase(hintId);
	else
	{
		if (openTail)
		{
			openTail = false;
			std::cout<<"[Error]"<<std::endl;
		}

		std::string errorInfo("Hint id ");
		errorInfo.append(std::to_string(hintId)).append(" cannot be found in 'allSplitHintIds' set.");
		errorInfos.push_back(errorInfo);
		std::cout<<errorInfo<<std::endl;
	}

	if (columsDefinitions.size())
	{
		std::vector<std::vector<std::string>> colums = ar.get("rows", std::vector<std::vector<std::string>>());
		if (columsDefinitions.size() != colums.size())
		{
			std::string errorInfo("Sub-table ");
			errorInfo.append(indexInfo).append(" (hintId: ");
			errorInfo.append(std::to_string(hintId)).append(") has different column count from sub-table hintId: ");
			errorInfo.append(std::to_string(standardTableIdx));

			if (openTail)
			{
				openTail = false;
				std::cout<<"[Error]"<<std::endl;
			}
			std::cout<<errorInfo<<std::endl;
			errorInfos.push_back(errorInfo);
		}
		else
		{
			bool columnOk = true;
			for (size_t j = 0; j < colums.size(); j++)
			{
				const std::vector<std::string>& row = colums[j];
				std::vector<std::string>& stdRow = columsDefinitions[j];

				for (int k = 0; k < (int)row.size(); k++)
				{
					if (row[k] != stdRow[k])
					{
						columnOk = false;
						std::string errorInfo("Sub-table ");
						errorInfo.append(indexInfo);
						errorInfo.append(" (hintId: ");
						errorInfo.append(std::to_string(hintId));
						errorInfo.append(") column '");
						errorInfo.append(row[k]);
						errorInfo.append("' is different from sub-table hintId: ");
						errorInfo.append(std::to_string(standardTableIdx));

						if (openTail)
						{
							openTail = false;
							std::cout<<"[Error]"<<std::endl;
						}
						std::cout<<errorInfo<<std::endl;
						errorInfos.push_back(errorInfo);
					}
				}
			}

			if (columnOk)
				std::cout<<"[OK]"<<std::endl;
		}
	
	}
	else
	{
		columsDefinitions = ar.get("rows", columsDefinitions);
		std::cout<<"[OK]"<<std::endl;
		standardTableIdx = hintId;
	}
}

void hashTableCheck(std::shared_ptr<TCPClient> client, const std::string& tableName, FPAReader &splitInfoAnswerReader)
{
	int tableCount = splitInfoAnswerReader.wantInt("tableCount");
	std::cout<<"Split type: Hash"<<std::endl;
	std::cout<<"Table Count: "<<tableCount<<std::endl;
	std::cout<<"Split Hint: "<<splitInfoAnswerReader.wantString("splitHint")<<std::endl;
	std::cout<<"----------------------------------"<<std::endl;

	std::set<int64_t> hintIds = getHintIds(client, tableName);
	if ((int)hintIds.size() != tableCount)
	{
		std::cout<<"Error!"<<std::endl;
		std::cout<<"Interface splitInfo return "<<tableCount<<" tables, but allSplitHintIds return "<<hintIds.size()<<" tables.\n";
		return;
	}

	std::string descSQL("desc ");
	descSQL.append(tableName);
	int standardTableIdx = 0;
	std::vector<std::string> errorInfos;
	std::vector<std::vector<std::string>> columsDefinitions;

	for (int i = 0; i < tableCount; i++)
		checkTable(client, i, tableName, descSQL, standardTableIdx, std::to_string(i), hintIds, columsDefinitions, errorInfos);

	printErrorInfos(errorInfos, hintIds);
}


void rangeTableCheck(std::shared_ptr<TCPClient> client, const std::string& tableName, FPAReader &splitInfoAnswerReader)
{
	int rangeCount = splitInfoAnswerReader.wantInt("count");
	std::string databaseCategory = splitInfoAnswerReader.wantString("databaseCategory");
	int rangeSpan = splitInfoAnswerReader.wantInt("span");
	std::cout<<"Split type: Range"<<std::endl;
	std::cout<<"Range Span: "<<rangeSpan<<std::endl;
	std::cout<<"Range Count: "<<rangeCount<<std::endl;
	std::cout<<"Database Category: "<<databaseCategory<<std::endl;
	std::cout<<"Split Hint: "<<splitInfoAnswerReader.wantString("splitHint")<<std::endl;

	bool secondarySplit = splitInfoAnswerReader.wantBool("secondarySplit");
	int secondarySplitSpan = 0;
	if (secondarySplit)
	{
		secondarySplitSpan = splitInfoAnswerReader.wantInt("secondarySplitSpan");
		std::cout<<"SecondarySplit: Yes"<<std::endl;
		std::cout<<"Secondary Split Span: "<<secondarySplitSpan<<std::endl;
	}
	else
		std::cout<<"SecondarySplit: No"<<std::endl;

	int totalRangeCount = rangeCount;
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
			totalRangeCount += oddEvenCount;
			std::cout<<"Total Range Span count "<<totalRangeCount<<", include "<<oddEvenCount<<" odd-even ranges."<<std::endl;
			std::cout<<"Odd-even ranges are : ";
			oddEvenRanges = ar.want("oddEvenIndexes", std::set<int>());
			for (int id: oddEvenRanges)
				std::cout<<id<<" ";
			std::cout<<"."<<std::endl;
		}
	}

	std::cout<<"----------------------------------"<<std::endl;
	int secondaryTableCountInFirstSpan = 1;
	int tableCount = totalRangeCount;
	if (secondarySplit)
	{
		int count = rangeSpan / secondarySplitSpan;
		if (rangeSpan % secondarySplitSpan)
			count += 1;
		
		tableCount *= count;
		secondaryTableCountInFirstSpan = count;
	}
	
	std::set<int64_t> hintIds = getHintIds(client, tableName);
	if ((int)hintIds.size() != tableCount)
	{
		std::cout<<"Error!"<<std::endl;
		std::cout<<"Interface allSplitHintIds return "<<hintIds.size()<<" tables, but "<<tableCount<<" tables is right.\n";
		return;
	}

	std::cout<<"----------------------------------"<<std::endl;

	std::string descSQL("desc ");
	descSQL.append(tableName);
	int standardTableIdx = 0;
	std::vector<std::string> errorInfos;
	std::vector<std::vector<std::string>> columsDefinitions;

	for (int i = 0; i < rangeCount; i++)
	{
		int rangeHintId = i * rangeSpan;

		if (oddEvenRanges.find(i) != oddEvenRanges.end())
		{
			if (secondarySplit)
			{
				std::string indexInfo;
				indexInfo.append(" in range ").append(std::to_string(i));

				for (int j = 0; j < secondaryTableCountInFirstSpan; j++)
				{
					std::string addedInfo(" secondary idx ");
					addedInfo.append(std::to_string(j));
					checkTable(client, rangeHintId + j * secondarySplitSpan    , tableName, descSQL, standardTableIdx, 
						indexInfo + addedInfo + " (even-sub)", hintIds, columsDefinitions, errorInfos);

					checkTable(client, rangeHintId + j * secondarySplitSpan + 1, tableName, descSQL, standardTableIdx, 
						indexInfo + addedInfo + " (odd-sub)" , hintIds, columsDefinitions, errorInfos);
				}
			}
			else
			{
				std::string indexInfo;
				indexInfo.append(" in range ").append(std::to_string(i));
				checkTable(client, rangeHintId    , tableName, descSQL, standardTableIdx, indexInfo + " (even-sub)", hintIds, columsDefinitions, errorInfos);
				checkTable(client, rangeHintId + 1, tableName, descSQL, standardTableIdx, indexInfo + " (odd-sub)" , hintIds, columsDefinitions, errorInfos);
			}
		}
		else
		{
			if (secondarySplit)
			{
				std::string indexInfo;
				indexInfo.append(" in range ").append(std::to_string(i));

				for (int j = 0; j < secondaryTableCountInFirstSpan; j++)
				{
					std::string addedInfo(" secondary idx ");
					addedInfo.append(std::to_string(j));
					checkTable(client, rangeHintId + j * secondarySplitSpan, tableName, descSQL, standardTableIdx, 
						indexInfo + addedInfo, hintIds, columsDefinitions, errorInfos);
				}
			}
			else
			{
				std::string indexInfo;
				indexInfo.append(" in range ").append(std::to_string(i));
				checkTable(client, rangeHintId, tableName, descSQL, standardTableIdx, indexInfo, hintIds, columsDefinitions, errorInfos);
			}
		}
	}

	printErrorInfos(errorInfos, hintIds);
}

void check(const std::string& host, int port, const std::string& tableName)
{
	std::shared_ptr<TCPClient> client = TCPClient::createClient(host, port);
	
	struct timeval start, finish;
	gettimeofday(&start, NULL);
	FPQWriter qw(1, "splitInfo");
	qw.param("tableName", tableName);

	std::cout<<std::endl<<"Query interface 'splitInfo' ..."<<std::endl;
	FPAnswerPtr answer = client->sendQuest(qw.take());
	FPAReader ar(answer);
	if (checkException(ar))
	{
		std::cout<<"----------------------------------"<<std::endl;
		if (ar.wantBool("splitByRange"))
			rangeTableCheck(client, tableName, ar);
		else
			hashTableCheck(client, tableName, ar);
	}
	
	gettimeofday(&finish, NULL);
	std::cout<<std::endl;
	timeCalc(start, finish);
}

int main(int argc, const char* argv[])
{
	if (argc == 4)
	{
		const char* host = argv[1];
		int port = atoi(argv[2]);
		const char* tablename = argv[3];

		ignoreSignals();
		check(host, port, tablename);
	}
	else
	{
		std::cout<<"Usage: "<<std::endl;
		std::cout<<"\t"<<argv[0]<<" DBProxy_host DBProxy_port table_name"<<std::endl;
	}
	
	return 0;
}
