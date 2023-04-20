g++ -std=c++11 -O3 -DNDEBUG -DTACO -I ../include -L../build/lib sddmm.cpp -o sddmm -ltaco
LD_LIBRARY_PATH=../build/lib ./sddmm