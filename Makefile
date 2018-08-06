all:
	$(pwd)
	g++ src/flight_cam.cpp -o cam -DTESTING -DSK_DEBUG -DRPI -std=c++11 -I./include/ -L./lib/ -lusb-1.0 -latikccd -lm -fopenmp -lboost_system -lboost_filesystem -lcfitsio -lpigpio -lrt -lpthread
