EXES_QUERY = DBQuery
EXES_PARAMS_QUERY = DBParamsQuery
EXES_TRANSACTION = DBTransaction
EXES_REFRESHER = DBRefresher
EXES_TABLE_CHECKER = DBTableChecker
EXES_TABLE_STRICT_CHECKER = DBTableStrictChecker
EXES_CONFIG_CONFUSER = DBConfigConfuser

FPNN_DIR = ../../../fpnn
DEPLOYMENT_DIR = ../../../deployment/dbproxy-cluster

MYSQL_CONFIG = mysql_config

#for MacOS
UNAME := $(shell uname -s)
ifeq ($(UNAME), Darwin)
	MYSQL_CONFIG = /usr/local/opt/mysql-client/bin/mysql_config
endif

CFLAGS +=
CXXFLAGS +=
CPPFLAGS += -I$(FPNN_DIR)/core -I$(FPNN_DIR)/proto -I$(FPNN_DIR)/base -I$(FPNN_DIR)/proto/msgpack -I$(FPNN_DIR)/proto/rapidjson `$(MYSQL_CONFIG) --cflags` -Wp,-U_FORTIFY_SOURCE
LIBS += -L$(FPNN_DIR)/extends -L$(FPNN_DIR)/core -L$(FPNN_DIR)/proto -L$(FPNN_DIR)/base -lfpnn `$(MYSQL_CONFIG) --libs_r`

OBJS_QUERY = DBQuery.o
OBJS_PARAMS_QUERY = DBParamsQuery.o
OBJS_TRANSACTION = DBTransaction.o
OBJS_REFRESHER = DBRefresher.o
OBJS_TABLE_CHECKER = DBTableChecker.o
OBJS_TABLE_STRICT_CHECKER = DBTableStrictChecker.o
OBJS_CONFIG_CONFUSER = DBConfigConfuser.o

all: $(EXES_QUERY) $(EXES_PARAMS_QUERY) $(EXES_REFRESHER) $(EXES_TRANSACTION) $(EXES_TABLE_CHECKER) $(EXES_TABLE_STRICT_CHECKER) $(EXES_CONFIG_CONFUSER)
	make -C DBDeployer

deploy:
	cp -rf $(EXES_QUERY) $(DEPLOYMENT_DIR)/tools/
	cp -rf $(EXES_PARAMS_QUERY) $(DEPLOYMENT_DIR)/tools/
	cp -rf $(EXES_TRANSACTION) $(DEPLOYMENT_DIR)/tools/
	cp -rf $(EXES_REFRESHER) $(DEPLOYMENT_DIR)/tools/
	cp -rf $(EXES_TABLE_CHECKER) $(DEPLOYMENT_DIR)/tools/
	cp -rf $(EXES_TABLE_STRICT_CHECKER) $(DEPLOYMENT_DIR)/tools/
	cp -rf $(EXES_CONFIG_CONFUSER) $(DEPLOYMENT_DIR)/tools/
	make deploy -C DBDeployer

clean:
	$(RM) *.o $(EXES_QUERY) $(EXES_PARAMS_QUERY) $(EXES_REFRESHER) $(EXES_TRANSACTION) $(EXES_TABLE_CHECKER) $(EXES_TABLE_STRICT_CHECKER) $(EXES_CONFIG_CONFUSER)
	make clean -C DBDeployer
	-$(RM) -rf *.dSYM
	
include $(FPNN_DIR)/def.mk
