.PHONY: all build clean run_taco run_hydride

all: clean build run_taco run_hydride

build: setup src/main.cpp
	g++ -std=c++11 -O3 -DNDEBUG -DTACO -I../../include -L../../build/lib src/main.cpp -o bin/main -ltaco

run_taco:
	LD_LIBRARY_PATH=../../build/lib ./bin/main taco > /dev/null

run_hydride:
	LD_LIBRARY_PATH=../../build/lib ./bin/main hydride > /dev/null
	
clean:
	rm -f bin/* tmp.txt *.tns

setup:
	mkdir -p bin/

#
# Generate shared lib
# clang -g -shared -o libmain.so -fPIC /tmp/taco_tmp_PLDJKC/jq2sijnfs66c.c bin/llvm_shim_tydride.ll bin/tydride.ll.legalize.ll
#
