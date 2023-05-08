#ifndef TACO_BACKEND_HYDRIDE_H
#define TACO_BACKEND_HYDRIDE_H
#include <map>
#include <vector>

#include "taco/ir/ir.h"
#include "taco/ir/ir_printer.h"
#include "codegen.h"

namespace taco {
namespace ir {

void hydride_generate_llvm_shim(const Stmt* stmt, std::string output_file);

void hydride_generate_llvm_bitcode(std::string input_file, std::string output_file, std::string benchmark_name);

} // namespace ir
} // namespace taco

#endif
