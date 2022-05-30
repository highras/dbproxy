dirs = DBProxy DBProxyMgr tools

all:
	for x in $(dirs); do (cd $$x; make -j4) || exit 1; done

clean:
	for x in $(dirs); do (cd $$x; make clean) || exit 1; done

deploy:
	-mkdir -p ../../deployment/dbproxy-cluster/bin/
	-mkdir -p ../../deployment/dbproxy-cluster/conf/
	-mkdir -p ../../deployment/dbproxy-cluster/tools/
	for x in $(dirs); do (cd $$x; make deploy) || exit 1; done