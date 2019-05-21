testprog:
	g++ -o testprog demo/atiktest.cpp -I./demo -I./include -latikccd -lusb-1.0 -lcfitsio

clib:
	g++ src/atikccd.cpp -fPIC -shared -o /usr/local/lib/libatik.so -latikccd -lusb-1.0

flight:
	$(pwd)
	g++ src/flight_cam.cpp -o cam -DPIC_TIME_GAP=15 -DSK_DEBUG -DRPI -DPIX_BIN=3 -std=c++11 -I./include/ -L./lib/ -lusb-1.0 -latikccd -lm -fopenmp -lboost_system -lboost_filesystem -lcfitsio -lpigpio -lrt -lpthread
	cp cam data/
	
clean:
	sudo rm -rf data/*
	rm cam
