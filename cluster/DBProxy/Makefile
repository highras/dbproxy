EXES_SERVER = DBProxy

FPNN_DIR = ../../../fpnn
DEPLOYMENT_DIR = ../../../deployment/dbproxy-cluster

DBProxyManagerFlag = 

MYSQL_CONFIG = mysql_config

#for MacOS
UNAME := $(shell uname -s)
ifeq ($(UNAME), Darwin)
	MYSQL_CONFIG = /usr/local/opt/mysql-client/bin/mysql_config
endif

CFLAGS += 
CXXFLAGS += $(DBProxyManagerFlag)
CPPFLAGS += -I$(FPNN_DIR)/core -I$(FPNN_DIR)/proto -I$(FPNN_DIR)/base -I$(FPNN_DIR)/proto/msgpack -I$(FPNN_DIR)/proto/rapidjson -I$(FPNN_DIR)/extends `$(MYSQL_CONFIG) --cflags` -Wp,-U_FORTIFY_SOURCE
LIBS += -L$(FPNN_DIR)/core -L$(FPNN_DIR)/proto -L$(FPNN_DIR)/base -lfpnn -L$(FPNN_DIR)/extends -lextends `$(MYSQL_CONFIG) --libs_r`

OBJS_SERVER = ConfigMonitor.o DataRouter.o DataRouterQuestProcessor.o MySQLClient.o MySQLTaskThreadPool.o SQLParser.o TableManager.o TableManagerBuilder.o TaskPackage.o TaskQueue.o

all: $(EXES_SERVER)

deploy:
	cp -rf $(EXES_SERVER) $(DEPLOYMENT_DIR)/bin/
	cp -rf DBProxy.conf $(DEPLOYMENT_DIR)/conf/

clean:
	$(RM) *.o $(EXES_SERVER)
	-$(RM) -rf *.dSYM
include $(FPNN_DIR)/def.mk
