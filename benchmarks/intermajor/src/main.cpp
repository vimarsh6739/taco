#include <random>
#include <iostream>
#include <cstring>
#include <taco.h>
#define ROWS 512
#define COLS 512
#define MID 512
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
  Format   rm({Dense, Dense});

  Tensor<int> C({ROWS,COLS}, rm);
  Tensor<int> B({ROWS,COLS},rm);

  for(int i=0;i<ROWS;++i){
    for(int j=0;j<COLS;++j){
      B.insert({i,j}, i + 1);
    }
  }

  for (int i = 0; i<ROWS; ++i) {
    for(int j=0;j<COLS;++j){
      C.insert({i,j}, i + 1);
    }
  }

  B.pack();
  C.pack();

  std::cout << "Done Loading" << std::endl;
  
  // Define output tensor
  Tensor<int> OutputCM({ROWS,COLS}, rm);
  Tensor<int> Output({ROWS,COLS},rm);

  // Index expressions
  IndexVar i("i"),j("j");
  OutputCM(i,j) = B(i,j) + C(i,j);
  Output(i,j) = B(i,j) + OutputCM(i,j);
  
  IndexStmt stmt = OutputCM.getAssignment().concretize();
  IndexStmt stmt2 = Output.getAssignment().concretize();

  // Vectorized schedule
  IndexVar i0("i0"),j0("j0");
  stmt = stmt.bound(i, i0, ROWS, BoundType::MaxExact)
             .bound(j, j0, COLS, BoundType::MaxExact)
             .parallelize(j0, ParallelUnit::CPUVector, OutputRaceStrategy::NoRaces);

  stmt2 = stmt2.bound(i, i0, ROWS, BoundType::MaxExact)
             .bound(j, j0, COLS, BoundType::MaxExact)
             .parallelize(j0, ParallelUnit::CPUVector, OutputRaceStrategy::NoRaces);

  OutputCM.compile(stmt, false, enableHydride);
  OutputCM.assemble();
  OutputCM.compute();

  // compute output now
  Output.compile(stmt2, false, enableHydride);
  Output.assemble();
  Output.compute();
  
  // Write the output of the computation to file (stored in the Matrix Market format).
  write(out_file, Output);
}
