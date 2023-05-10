// On Linux and MacOS, you can compile and run this program like so:
//   g++ -std=c++11 -O3 -DNDEBUG -DTACO -I ../../include -L../../build/lib sddmm.cpp -o sddmm -ltaco
//   LD_LIBRARY_PATH=../../build/lib ./sddmm
#include <random>
#include <iostream>
#include "taco.h"
using namespace taco;
int main(int argc, char* argv[]) {

  // Preprocessing
  if(argc < 2){
    std::cout << "Insufficient number of arguments provided. Usage: ./main [taco | hydride]"<<std::endl;
    return 0;
  }

  std::string compute_target = argv[1];
  bool enableHydride = false;
  std::string out_file = "out_" + compute_target + ".tns";
  if(compute_target == "hydride"){enableHydride = true;}

  std::default_random_engine gen(0);
  std::uniform_real_distribution<double> unif(0.0, 1.0);

  Format sparrrse({Sparse});
  Format   rm({Dense});

//   Tensor<double> B = read("webbase-1M/webbase-1M.mtx", dcsr);
  // Generate a random dense matrix and store it in row-major (dense) format.
  Tensor<int> C({512}, rm);
  Tensor<int> B({512}, rm);
  for (int i = 0; i < C.getDimension(0); ++i) {
    C.insert({i}, i + 1);
    B.insert({i}, i + 1);
  }
  B.pack();
  C.pack();
  std::cout << "done loading" << std::endl;


  Tensor<int> A({512}, rm);
  Tensor<int> A2(0);

  // Define the SDDMM computation using index notation.
  IndexVar i("original_i"); 
  A(i) = B(i) + C(i);
  A2 = sum(i, A(i));
  IndexStmt stmt = A.getAssignment().concretize();

  IndexVar i0("outer_i"),i1("inner_i");
  // stmt = stmt.split(i,i0,i1,64).parallelize(i1,ParallelUnit::CPUVector,OutputRaceStrategy::NoRaces).unroll(i1,4);  
  stmt = stmt.bound(i, i0, 512, BoundType::MaxExact).parallelize(i0, ParallelUnit::CPUVector, OutputRaceStrategy::NoRaces);


  A.compile(stmt, false, enableHydride);
  std::cout << "done compiling" << std::endl;


  A.assemble();
  std::cout << "done assembling" << std::endl;

  A.compute();
  std::cout << "done computing" << std::endl;
  A2.compile(enableHydride);
  A2.assemble();

  // auto start = std::chrono::system_clock::now();
  A2.compute();
  // auto end = std::chrono::system_clock::now();
  // std::cout << "Compilation took " << (end - start).count() << "seconds." << std::endl;

  std::cout << "done computing A2" << std::endl;
  std::cout << A2.at(std::vector<int>(0)) << std::endl;
  // Write the output of the computation to file (stored in the Matrix Market format).
  // write("A.mtx", A);
  write(out_file, A2);
}