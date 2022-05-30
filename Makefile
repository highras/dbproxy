dirs = standard cluster

all:
	for x in $(dirs); do (cd $$x; make) || exit 1; done

clean:
	for x in $(dirs); do (cd $$x; make clean) || exit 1; done

deploy:
	for x in $(dirs); do (cd $$x; make deploy) || exit 1; done