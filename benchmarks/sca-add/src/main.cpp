// On Linux and MacOS, you can compile and run this program like so:
//   g++ -std=c++11 -O3 -DNDEBUG -DTACO -I ../../include -L../../build/lib sddmm.cpp -o sddmm -ltaco
//   LD_LIBRARY_PATH=../../build/lib ./sddmm
#include <random>
#include <iostream>
#include "taco.h"
using namespace taco;
int main(int argc, char* argv[]) {
  std::default_random_engine gen(0);
  std::uniform_real_distribution<double> unif(0.0, 1.0);
  // Predeclare the storage formats that the inputs and output will be stored as.
  // To define a format, you must specify whether each dimension is dense or sparse
  // and (optionally) the order in which dimensions should be stored. The formats
  // declared below correspond to doubly compressed sparse row (dcsr), row-major
  // dense (rm), and column-major dense (dm).
  // Format dcsr({Dense,Dense});
  Format   rm({Dense});
  // Format   cm({Dense,Dense}, {1,0});

  // Load a sparse matrix from file (stored in the Matrix Market format) and
  // store it as a doubly compressed sparse row matrix. Matrices correspond to
  // order-2 tensors in taco. The matrix in this example can be download from:
  // https://www.cise.ufl.edu/research/sparse/MM/Williams/webbase-1M.tar.gz
    std::cout << "done preloading" << std::endl;

//   Tensor<double> B = read("webbase-1M/webbase-1M.mtx", dcsr);
  // Generate a random dense matrix and store it in row-major (dense) format.
  Tensor<int> C({4}, rm);
  Tensor<int> B({4},rm);
  for (int i = 0; i < C.getDimension(0); ++i) {
    C.insert({i}, i + 1);
    B.insert({i}, i + 1);
  }
  B.pack();
  C.pack();
  std::cout << "done loading" << std::endl;

//   // Generate another random dense matrix and store it in column-major format.
//   Tensor<double> D({2, B.getDimension(1)}, rm);
//   for (int i = 0; i < D.getDimension(0); ++i) {
//     for (int j = 0; j < D.getDimension(1); ++j) {
//       D.insert({i,j}, unif(gen));
//     }
//   }
//   D.pack();

  // Declare the output matrix to be a sparse matrix with the same dimensions as
  // input matrix B, to be also stored as a doubly compressed sparse row matrix.
  Tensor<int> A({4}, rm);

  // Define the SDDMM computation using index notation.
  IndexVar i("i");
  A(i) = B(i) + C(i) + 10;

  // IndexVar i, j, k;
  // A(i,j) = B(i,k) * C(k,j);

  // Tensor<double> D({2,2}, rm);
  // D(i,j) = A(i,j) + B(i,j);

  // At this point, we have defined how entries in the output matrix should be
  // computed from entries in the input matrices but have not actually performed
  // the computation yet. To do so, we must first tell taco to generate code that
  // can be executed to compute the SDDMM operation.


  // A.parallelize(i,CPUVector);

  IndexStmt stmt = A.getAssignment().concretize();

  IndexVar j("j");
  stmt = stmt.bound(i, j, 4, BoundType::MaxExact)
             .parallelize(j, ParallelUnit::CPUVector, OutputRaceStrategy::NoRaces);

  A.compile(stmt, false, true);
  std::cout << "done compiling" << std::endl;

  // std::string path = A.emitHydride();
  // std::cout << "emitted hydride @ " << path << std::endl;

  // std::cout << A.getSource() << std::endl;


  // We can now call the functions taco generated to assemble the indices of the
  // output matrix and then actually compute the SDDMM.
  A.assemble();
  std::cout << "done assembling" << std::endl;

  A.compute();
  std::cout << "done computing" << std::endl;


  // Write the output of the computation to file (stored in the Matrix Market format).
  // write("A.mtx", A);
}