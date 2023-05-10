#include <random>
#include <iostream>
#include <cstring>
#include <taco.h>
#define ROWS 1024
#define COLS 128
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
  Format cm({Dense,Dense},{1,0});
  Format rrr({Dense,Dense,Dense});

  Tensor<int> C({MID,COLS}, cm);
  Tensor<int> B({ROWS,MID},rm);

  for(int i=0;i<ROWS;++i){
    for(int j=0;j<MID;++j){
      B.insert({i,j}, i + 1);
    }
  }

  for (int i = 0; i<MID; ++i) {
    for(int j=0;j<COLS;++j){
      C.insert({i,j}, i + 1);
    }
  }

  B.pack();
  C.pack();

  std::cout << "Done Loading" << std::endl;
  
  // Define output tensor
  Tensor<int> OuterProd({ROWS,COLS,MID}, rrr);
  Tensor<int> Output({ROWS,COLS},rm);

  // Index expressions
  IndexVar i("i"),j("j"),k("k");
  OuterProd(i,j,k) = B(i,k) * C(k,j);
  Output(i,j) = sum(k,OuterProd(i,j,k));

  IndexStmt stmt = OuterProd.getAssignment().concretize();

  // Vectorized schedule
  IndexVar i0("i0"),j0("j0"),k0("k0");
  stmt = stmt.bound(i, i0, ROWS, BoundType::MaxExact)
             .bound(j, j0, COLS, BoundType::MaxExact)
             .bound(k, k0, MID, BoundType::MaxExact)
             .parallelize(k0, ParallelUnit::CPUVector, OutputRaceStrategy::NoRaces);

  OuterProd.compile(stmt, false, enableHydride);
  std::cout << "DONE COMPILING" << std::endl;

  OuterProd.assemble();
  std::cout << "DONE ASSEMBLING" << std::endl;

  OuterProd.compute();
  std::cout << "DONE COMPUTING" << std::endl;

  Output.compile(enableHydride);
  Output.assemble();
  Output.compute();
  
  // Write the output of the computation to file (stored in the Matrix Market format).
  write(out_file, Output);
}
