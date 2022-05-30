#include <iostream>
#include <string.h>
#include <stdlib.h>
#include "ignoreSignals.h"
#include "TCPClient.h"

using namespace fpnn;

void refresh(const char *endpoint)
{
	const char* colon = strchr(endpoint, ':');
	if (!colon)
	{
		std::cout<<"  invalid endpoint "<<endpoint<<std::endl;
		return;
	}
	std::string host(endpoint, colon - endpoint);
	int port = atoi(colon + 1);

	std::shared_ptr<TCPClient> client = TCPClient::createClient(host, port);
	FPQuestPtr quest = FPQWriter::emptyQuest("refresh");
	FPAnswerPtr answer = client->sendQuest(quest);
	if (answer->status())
		std::cout<<"Refresh error!"<<std::endl;
	else
		std::cout<<"Refresh finished."<<std::endl;
}

int main(int argc, const char* argv[])
{
	if (argc < 2)
	{
		std::cout<<"Usage: "<<std::endl;
		std::cout<<"\t"<<argv[0]<<" host:port ..."<<std::endl;
		return 0;
	}

	ignoreSignals();
	for (int i = 1; i < argc; i++)
		refresh(argv[i]);

	return 0;
}
