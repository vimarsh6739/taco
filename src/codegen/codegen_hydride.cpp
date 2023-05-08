#include "codegen_hydride.h"

#include <chrono>
#include <iostream>
#include <fstream>
#include "rosette.h"

using namespace std;

namespace taco {
namespace ir {

class LLVMShimEmitter : public IRVisitor {
 public:
  LLVMShimEmitter(std::ostream& stream, std::string file_name) : stream(stream) { emit_llvm_header(file_name); };

  void visit(const Stmt* op) { op->accept(this); }

 protected:
  using IRVisitor::visit;
  std::ostream& stream;

  void emit_llvm_header(std::string file_name) {
    stream << "; ModuleID = '" << file_name << "'" << std::endl
           << "source_filename = \"" << file_name << "\"" << std::endl
           << "target datalayout = \"e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128\"" << std::endl
           << "target triple = \"unknown-unknown-unknown\"" << std::endl
           << "attributes #0 = {alwaysinline}" << std::endl
           << std::endl;
  }

  std::string get_type(const Expr& arg) {
    const Datatype& type = arg.type();
    int bits = type.getNumBits();
    if (type.isUInt() || type.isInt())
      return "i" + std::to_string(bits);
    else if (type.isFloat() && bits == 32)
      return "float";
    else if (type.isFloat() && bits == 64)
      return "double";
    taco_ierror << "Invalid llvm type: " << type;
    return "";
  }

  void emit_llvm_function(const Store* op, std::string name, const std::vector<Expr>& args) {
    std::string dest_type = get_type(Load::make(op->arr, op->loc, op->vector_width));
    std::string extern_name = name;
    std::replace(extern_name.begin(), extern_name.end(), '_', '.');

    stream << "; declare the generated hydride function" << std::endl
           << "declare <" << op->vector_width << " x " << dest_type << "> @" << extern_name << " (";

    for (size_t i = 0; i < args.size(); ++i) {
      if (i > 0)
        stream << ", ";
      if (isa<Load>(args[i])) {
        const Load* arg = args[i].as<Load>();
        stream << "<" << arg->vector_width << " x " << get_type(arg) << ">";
      } else {
        const Var* arg = args[i].as<Var>();
        stream << get_type(arg);
      }
    }

    stream << ")" << std::endl
           << std::endl;

    stream << "define void @shim_" << name << "(";

    // emit destination argument
    stream << dest_type << "* %dst";

    // emit other arguments
    for (size_t i = 0; i < args.size(); ++i)
      stream << ", " << get_type(args[i]) << (isa<Load>(args[i])? "*": "") << " %reg_" << i;

    stream << ") #0 {" << std::endl << std::endl;

    stream << '\t' << "; load dst" << std::endl
           << '\t' << "%gep_dst = getelementptr " << dest_type << ", " << dest_type << "* %dst, " << dest_type << " 0" << std::endl
           << '\t' << "%vect_dst_ptr = bitcast " << dest_type << "* %gep_dst to <" << op->vector_width << " x " << dest_type << ">*" << std::endl
           << std::endl;

    for (size_t i = 0; i < args.size(); ++i) {
      if (isa<Load>(args[i])) {
        const Load* arg = args[i].as<Load>();
        std::string arg_type = get_type(arg);
        stream << '\t' << "; load reg_" << i << std::endl
               << '\t' << "%gep_reg_" << i << " = getelementptr " << arg_type << ", " << arg_type << "* %reg_" << i << ", " << arg_type << " 0" << std::endl
               << '\t' << "%vect_reg_" << i << "_ptr = bitcast " << arg_type << "* %gep_reg_" << i << " to <" << arg->vector_width << " x " << arg_type << ">*" << std::endl
               << '\t' << "%vect_reg_" << i << " = load <" << arg->vector_width << " x " << arg_type << ">, <" << arg->vector_width << " x " << arg_type << ">* %vect_reg_" << i << "_ptr" << std::endl
               << std::endl;
      }
    }

    stream << '\t' << "; extern call to hydride generated function" << std::endl
           << '\t' << "%ret_val = call <" << op->vector_width << " x " << dest_type << "> @" << extern_name << "(";

    for (size_t i = 0; i < args.size(); ++i) {
      if (i > 0)
        stream << ", ";
      if (isa<Load>(args[i])) {
        const Load* arg = args[i].as<Load>();
        stream << "<" << arg->vector_width << " x " << get_type(arg) << ">";
      } else {
        const Var* arg = args[i].as<Var>();
        stream << get_type(arg);
      }
      stream << " %vect_reg_" << i;
    }

    stream << ")" << std::endl
           << std::endl;

    stream << '\t' << "store volatile <" << op->vector_width << " x " << dest_type << "> %ret_val, <" << op->vector_width << " x " << dest_type << ">* %vect_dst_ptr, align 1" << std::endl
           << std::endl;

    stream << '\t' << "ret void" << std::endl 
           << "}" << std::endl 
           << std::endl;
  }

  void visit(const Store* op) override {
    // Check that the rhs of the store is a call
    if (!isa<Call>(op->data))
      return IRVisitor::visit(op);

    // Check that the call is to an extern llvm function
    const Call* call = op->data.as<Call>();
    if (!call->extern_llvm)
      return IRVisitor::visit(op);
    
    emit_llvm_function(op, call->func, call->args);
  }

};


void hydride_generate_llvm_shim(const Stmt* stmt, std::string output_file) {
  // Emit llvm shim to the hydride generated .ll files.
  std::ofstream ostream;
  ostream.open(output_file);
  LLVMShimEmitter(ostream, output_file).visit(stmt);
  ostream.close();

  std::cout << "Wrote llvm shim to file @ " << output_file << std::endl;
}


void hydride_generate_llvm_bitcode(std::string input_file, std::string output_file, std::string benchmark_name) {

    std::string target_flag = "-x86-hydride-legalize";

    const char* hydride_src = getenv("HYDRIDE_ROOT");
    assert(hydride_src && "HYDRIDE_ROOT environment variable needs to be defined for codegen");

    const char* legalizer_so = getenv("LEGALIZER_PATH");
    assert(legalizer_so && "LEGALIZER_PATH environment variable must be defined for hydride codegen");

    const char* intrin_wrapper = getenv("INTRINSICS_LL");
    assert(intrin_wrapper && "INTRINSICS_LL must be defined for hydride llvm code-gen");

    std::string cmd = "python " + std::string(hydride_src)
                    + "/codegen-generator/tools/low-level-codegen/RoseLowLevelCodeGen.py "
                    + input_file + " "
                    + std::string(legalizer_so) + " "
                    + std::string(intrin_wrapper) + " "
                    + target_flag + " "
                    + output_file;
    
    auto start = std::chrono::system_clock::now();
    int ret_code = system(cmd.c_str());
    taco_iassert(ret_code == 0) << "Codegen crashed, exiting ...";

    auto end = std::chrono::system_clock::now();
    std::cout << "Compilation took " << (end - start).count() << "seconds." << std::endl;

    // TEMP CMD
    std::string temp_cmd = "cp /tmp/" + benchmark_name + ".ll.legalize.ll " + output_file;
    ret_code = system(temp_cmd.c_str());
    taco_iassert(ret_code == 0) << "Copying crashed, exiting ...";

    std::cout << "Wrote lifted llvm to file @ " << output_file << std::endl;
}


}
}
