all:
	g++ src/flight_cam.cpp -o cam -DSK_DEBUG -std=c++11 -Iinclude/ -Llib/-lusb-1.0 -latikccd -lgzstream -lz -lm -fopenmp
