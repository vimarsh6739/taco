.PHONY: all build clean run

all: build run

build: src/main.cpp
	g++ -std=c++11 -O3 -DNDEBUG -DTACO -I ../../include -L../../build/lib src/main.cpp -o bin/main -ltaco

run:
	LD_LIBRARY_PATH=../../build/lib ./bin/main

clean:
	rm bin/*

#
# Generate shared lib
# clang -g -shared -o libmain.so -fPIC /tmp/taco_tmp_PLDJKC/jq2sijnfs66c.c bin/llvm_shim_tydride.ll bin/tydride.ll.legalize.ll
#
