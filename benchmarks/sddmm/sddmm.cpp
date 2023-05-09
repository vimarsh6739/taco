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
  Format dcsr({Sparse,Sparse});
  Format   rm({Dense,Dense});
  Format   cm({Dense,Dense}, {1,0});

  // Load a sparse matrix from file (stored in the Matrix Market format) and
  // store it as a doubly compressed sparse row matrix. Matrices correspond to
  // order-2 tensors in taco. The matrix in this example can be download from:
  // https://www.cise.ufl.edu/research/sparse/MM/Williams/webbase-1M.tar.gz
  Tensor<int32_t> B({10, 10}, dcsr);
  B.insert({1, 2}, 1);
  B.insert({1, 3}, 1);
  B.insert({1, 4}, 1);
  B.insert({1, 5}, 1);
  B.insert({2, 5}, 1);
  B.insert({3, 3}, 1);
  B.insert({4, 4}, 1);
  B.insert({5, 5}, 1);
  B.insert({8, 8}, 1);
  B.insert({8, 9}, 1);

  // Generate a random dense matrix and store it in row-major (dense) format.
  Tensor<int32_t> C({B.getDimension(0), 10}, rm);
  for (int i = 0; i < C.getDimension(0); ++i) {
    for (int j = 0; j < C.getDimension(1); ++j) {
      C.insert({i,j}, static_cast<int>(unif(gen) * 2));
    }
  }
  C.pack();

  // Generate another random dense matrix and store it in column-major format.
  Tensor<int32_t> D({10, B.getDimension(1)}, cm);
  for (int i = 0; i < D.getDimension(0); ++i) {
    for (int j = 0; j < D.getDimension(1); ++j) {
      D.insert({i,j}, static_cast<int>(unif(gen) * 2));
    }
  }
  D.pack();

  // Declare the output matrix to be a sparse matrix with the same dimensions as
  // input matrix B, to be also stored as a doubly compressed sparse row matrix.
  Tensor<int32_t> A(B.getDimensions(), dcsr);

  // Define the SDDMM computation using index notation.
  IndexVar i, j, k;
  A(i,j) = B(i,j) * C(i,k) * D(k,j);

  // At this point, we have defined how entries in the output matrix should be
  // computed from entries in the input matrices but have not actually performed
  // the computation yet. To do so, we must first tell taco to generate code that
  // can be executed to compute the SDDMM operation.
  A.compile(true);
  // We can now call the functions taco generated to assemble the indices of the
  // output matrix and then actually compute the SDDMM.
  A.assemble();
  A.compute();
  // Write the output of the computation to file (stored in the Matrix Market format).
  write("A.mtx", A);
}