#include <random>
#include <iostream>
#include <cstring>
#include <taco.h>
#define ROWS 1024 // no of images
#define COLS 784  // flattened image vector(approx)
#define NHIDDEN 256  
#define NOUT 16
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

  Tensor<int> Input({ROWS,COLS}, rm);
  Tensor<int> W1({COLS,NHIDDEN},cm);
  Tensor<int> W2({NHIDDEN,NOUT},cm);

  for(int i=0;i<ROWS;++i){
    for(int j=0;j<COLS;++j){
      Input.insert({i,j}, i + 1);
    }
  }

  for (int i = 0; i<COLS; ++i) {
    for(int j=0;j<NHIDDEN;++j){
      W1.insert({i,j}, i + j + 1);
    }
  }

  for (int i = 0; i<NHIDDEN; ++i) {
    for(int j=0;j<NOUT;++j){
      W2.insert({i,j}, -i + j - 1 + NHIDDEN);
    }
  }

  Input.pack();
  W1.pack();
  W2.pack();

  std::cout << "Done Loading" << std::endl;
  
  // Define output tensors
  Tensor<int> OuterProd1({ROWS,NHIDDEN,COLS}, rrr);
  Tensor<int> OuterProd2({ROWS,NOUT,NHIDDEN}, rrr);
  Tensor<int> Output1({ROWS,NHIDDEN},rm);
  Tensor<int> Output2({ROWS,NOUT},rm);

  // Index expressions
  IndexVar i("i"),j("j"),k("k");
  IndexVar ii("ii"),jj("jj"),kk("kk");

  OuterProd1(i,j,k) = Input(i,k) * W1(k,j);
  Output1(i,j) = sum(k,OuterProd1(i,j,k));
  OuterProd2(ii,jj,kk) = Output1(ii,kk) * W2(kk,jj);
  Output2(ii,jj) = sum(kk,OuterProd2(ii,jj,kk));

  IndexStmt stmt = OuterProd1.getAssignment().concretize();
  IndexStmt stmt2 = OuterProd2.getAssignment().concretize();

  // Vectorized schedule
  IndexVar i0("i0"),j0("j0"),k0("k0");
  IndexVar ii0("ii0"),jj0("jj0"),kk0("kk0");
  stmt = stmt.bound(i, i0, ROWS, BoundType::MaxExact)
             .bound(j, j0, NHIDDEN, BoundType::MaxExact)
             .bound(k, k0, COLS, BoundType::MaxExact)
             .parallelize(k0, ParallelUnit::CPUVector, OutputRaceStrategy::NoRaces);

  stmt2 = stmt2.bound(ii, ii0, ROWS, BoundType::MaxExact)
             .bound(jj, jj0, NOUT, BoundType::MaxExact)
             .bound(kk, kk0, NHIDDEN, BoundType::MaxExact)
             .parallelize(kk0, ParallelUnit::CPUVector, OutputRaceStrategy::NoRaces);

  OuterProd1.compile(stmt, false, enableHydride);
  OuterProd1.assemble();
  OuterProd1.compute();

  Output1.compile(enableHydride);
  Output1.assemble();
  Output1.compute();
  std::cout << "Finished computing first layer" << std::endl;

  OuterProd2.compile(stmt2, false, enableHydride);
  OuterProd2.assemble();
  OuterProd2.compute();

  Output2.compile(enableHydride);
  Output2.assemble();
  Output2.compute();
    
  std::cout << "Finished computing second layer" << std::endl;

  // Write the output of the computation to file (stored in the Matrix Market format).
  write(out_file, Output2);
}
