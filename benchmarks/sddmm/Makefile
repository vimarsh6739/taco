
.PHONY: all build clean run

all: build run

build: sddmm.cpp
	g++ -std=c++11 -O3 -DNDEBUG -DTACO -I ../../include -L../../build/lib sddmm.cpp -o sddmm -ltaco

run:
	LD_LIBRARY_PATH=../../build/lib ./sddmm

clean:
	rm sddmm
