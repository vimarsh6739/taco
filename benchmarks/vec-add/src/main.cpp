#include <random>
#include <iostream>
#include <cstring>
#include <taco.h>
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

  // Begin program
  std::default_random_engine gen(0);
  std::uniform_real_distribution<double> unif(0.0, 1.0);
  Format   rm({Dense});
  Tensor<int> C({1024}, rm);
  Tensor<int> B({1024},rm);
  for (int i = 0; i < 1024; ++i) {
    C.insert({i}, i + 1);
    B.insert({i}, i + 1);
  }
  B.pack();
  C.pack();
  std::cout << "Done Loading" << std::endl;
  
  // Define output tensor
  Tensor<int> A({1024}, rm);

  // Index expressions
  IndexVar i("i");
  A(i) = B(i) + C(i) + 1;
  
  
  IndexStmt stmt = A.getAssignment().concretize();

  // Vectorized schedule
  IndexVar i0("i0");
  stmt = stmt.bound(i, i0, 1024, BoundType::MaxExact)
             .parallelize(i0, ParallelUnit::CPUVector, OutputRaceStrategy::NoRaces);

  A.compile(stmt, false, enableHydride);
  std::cout << "DONE COMPILING" << std::endl;

  A.assemble();
  std::cout << "DONE ASSEMBLING" << std::endl;

  A.compute();
  std::cout << "DONE COMPUTING" << std::endl;

  // Write the output of the computation to file (stored in the Matrix Market format).
  write(out_file, A);
}
