.PHONY: all build clean run

all: build run

build: src/main.cpp
	g++ -std=c++11 -O3 -DNDEBUG -DTACO -I ../../include -L../../build/lib src/main.cpp -o bin/main -ltaco

run:
	LD_LIBRARY_PATH=../../build/lib ./bin/main

clean:
	rm bin/*

#
#
# Optional - Inlining optimization:
# 
# Generate shared lib
# 
