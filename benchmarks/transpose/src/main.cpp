#include <random>
#include <iostream>
#include <cstring>
#include <taco.h>
using namespace taco;
#define ROWS 160
#define COLS 256
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

  Tensor<int> Input({ROWS,COLS},rm);
  for (int i = 0; i<ROWS; ++i) {
    for(int j=0;j<COLS;++j){
      Input.insert({i,j}, ROWS - i - 1 + j);
    }
  }
  Input.pack();
  std::cout << "Done Loading" << std::endl;
  
  // Define output tensors
  Tensor<int> Output({COLS,ROWS},rm);

  // Index expressions
  IndexVar i("i"),j("j");
  Output(j,i) = Input(i,j);

  IndexStmt stmt = Output.getAssignment().concretize();

  // Vectorized schedule
  IndexVar i0("i0"),j0("j0");
  stmt = stmt.bound(i, i0, ROWS, BoundType::MaxExact)
             .bound(j, j0, COLS, BoundType::MaxExact)
             .parallelize(i0, ParallelUnit::CPUVector, OutputRaceStrategy::NoRaces);

  Output.compile(stmt, false, enableHydride);
  std::cout << "DONE COMPILING" << std::endl;

  Output.assemble();
  std::cout << "DONE ASSEMBLING" << std::endl;

  Output.compute();
  std::cout << "DONE COMPUTING" << std::endl;
  
  // Write the output of the computation to file (stored in the Matrix Market format).
  write(out_file, Output);
}
