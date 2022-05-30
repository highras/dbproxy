#include <iostream>
#include "TCPEpollServer.h"
#include "DataRouterQuestProcessor.h"

int main(int argc, char* argv[])
{
	try{
		if (argc != 2)
		{
			std::cout<<"Usage: "<<argv[0]<<" config"<<std::endl;
			return 0;
		}
		if(!Setting::load(argv[1])){
			std::cout<<"Config file error:"<< argv[1]<<std::endl;
			return 1;
		}

		ServerPtr server = TCPEpollServer::create();
		server->setQuestProcessor(std::make_shared<DataRouterQuestProcessor>());
		if (server->startup())
			server->run();
	}
	catch(const std::exception& ex){
		std::cout<<"exception:"<<ex.what()<<std::endl;
	}   
	catch(...){
		std::cout<<"Unknow exception."<<std::endl;
	}   

	return 0;
}
