BUILD_PARAMS = -e EXES_SERVER=DBProxyMgr -e FPNN_DIR=../../../../fpnn -e DEPLOYMENT_DIR=../../../../deployment/debproxy-standard -e DBProxyManagerFlag=-DDBProxy_Manager_Version

all:
	mkdir -p DBPM
	cp -f ../DBProxy/*.h DBPM/
	cp -f ../DBProxy/*.cpp DBPM/
	cp -f ../DBProxy/DBProxy.conf DBPM/
	cp -f ../DBProxy/Makefile DBPM/
	make -C DBPM $(BUILD_PARAMS)

deploy:
	make -C DBPM deploy $(BUILD_PARAMS)

clean:
	make -C DBPM clean $(BUILD_PARAMS)

cleanall:
	$(RM) -rf DBPM