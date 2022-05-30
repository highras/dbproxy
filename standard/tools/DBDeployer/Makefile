EXES_CLIENT = DBDeployer

FPNN_DIR = ../../../../fpnn
DEPLOYMENT_DIR = ../../../../deployment/dbproxy-standard

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

OBJS_CLIENT = DBDeployer.o DBDeployer.TableOperations.o DBDeployer.Confuser.o MySQLClient.o linenoise.o CmdParser.o CommandProcessor.o

all: $(EXES_CLIENT)

deploy:
	cp -rf $(EXES_CLIENT) $(DEPLOYMENT_DIR)/tools/

clean:
	$(RM) *.o $(EXES_CLIENT)
	-$(RM) -rf *.dSYM

include $(FPNN_DIR)/def.mk
