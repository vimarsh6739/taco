#include <iostream>
#include <fstream>
#include <dlfcn.h>
#include <algorithm>
#include <unordered_set>
#include <taco.h>
#include <chrono>

#include "taco/ir/ir_printer.h"
#include "taco/ir/ir_visitor.h"
#include "taco/ir/ir_rewriter.h"
#include "rosette.h"
#include "taco/error.h"
#include "taco/util/strings.h"
#include "taco/util/collections.h"

using namespace std;

namespace taco {
namespace ir {

// Some helper functions
namespace {

class HydrideEmitter : public IRVisitor {
  // Visits a Taco IR expression and converts it to a hydride IR expression.
  // This is done in Rosette syntax.

 public:
  // map from taco load instructions to racket registers
  std::map<const Load*, uint> loadToRegMap;

  // map from taco variables to racket registers
  std::map<const Var*, uint> varToRegMap;

  HydrideEmitter(std::ostream& stream) : stream(stream) {};

  void translate(const Expr* op, std::string benchmark_name, size_t expr_id, size_t vector_width) {
    bitwidth = 512;
    this->vector_width = vector_width;

    emit_racket_imports();
    stream << std::endl;
    emit_racket_debug();
    stream << std::endl;
    emit_set_current_bitwidth();
    stream << std::endl;
    emit_set_memory_limit(20000);
    stream << std::endl;

    LoadVarMapVisitor(loadToRegMap, varToRegMap).visit(op);
    emit_symbolic_buffers();
    stream << std::endl;
    emit_buffer_id_map();
    stream << std::endl;

    emit_expr(op);
    stream << std::endl;

    emit_hydride_synthesis(/* expr_depth */ 3, /* VF */ vector_width);
    stream << std::endl;
    emit_compile_to_llvm(benchmark_name, expr_id);
    stream << std::endl;
    emit_write_synth_log_to_file(benchmark_name, expr_id);
    stream << std::endl;
  }

 protected:
  using IRVisitor::visit;

  std::ostream& stream;
  std::string benchmark_name;
  size_t expr_count;
  size_t bitwidth;
  size_t vector_width = 1;  // Vector width of the current expression.

  std::map<Expr, std::string, ExprCompare> varMap;
  std::vector<Expr> localVars;

  class LoadVarMapVisitor : public IRVisitor {
   public:
    LoadVarMapVisitor(std::map<const Load*, uint>& loadToRegMap, std::map<const Var*, uint>& varToRegMap) 
                    : loadToRegMap(loadToRegMap), varToRegMap(varToRegMap) {}

    void visit(const Expr* op) { op->accept(this); }

   protected:
    using IRVisitor::visit;

    // map from taco load instructions to racket registers
    std::map<const Load*, uint>& loadToRegMap;

    // map from taco variables to racket registers
    std::map<const Var*, uint>& varToRegMap;

    void visit(const Var* op) override {
      if (varToRegMap.find(op) == varToRegMap.end()) {
        size_t reg_counter = varToRegMap.size() + loadToRegMap.size();
        varToRegMap[op] = reg_counter;
      }
    }

    void visit(const Load* op) override {
      if (loadToRegMap.find(op) == loadToRegMap.end()) {
        size_t reg_counter = varToRegMap.size() + loadToRegMap.size();
        loadToRegMap[op] = reg_counter;
      }
    }
  };

  void emit_racket_imports() {
    stream << "#lang rosette" << std::endl
           << "(require rosette/lib/synthax)" << std::endl
           << "(require rosette/lib/angelic)" << std::endl
           << "(require racket/pretty)" << std::endl
           << "(require data/bit-vector)" << std::endl
           << "(require rosette/lib/destruct)" << std::endl
           << "(require rosette/solver/smt/boolector)" << std::endl
           << "(require hydride)" << std::endl;
  }

  void emit_racket_debug() {
    stream << ";; Uncomment the line below to enable verbose logging" << std::endl
           << "(enable-debug)" << std::endl;
  }

  void emit_set_current_bitwidth() {
    stream << "(current-bitwidth " << bitwidth << ")";
  }

  void emit_set_memory_limit(size_t mb) {
    stream << "(custodian-limit-memory (current-custodian) (* " << mb << " 1024 1024))" << std::endl;
  }

  void emit_symbolic_buffer(uint reg_num, Datatype type, size_t width) {
    std::string reg_name = "reg_" + std::to_string(reg_num);
    size_t bitwidth = type.getNumBits() * width;

    stream << "(define " << reg_name << "_bitvector (bv 0 (bitvector " << bitwidth << ")))" << std::endl
           << "(define " << reg_name << " (halide:create-buffer " << reg_name << "_bitvector '";
    
    int bits = type.getNumBits();
    if (type.isUInt())
      stream << "uint" << bits;
    else if (type.isInt())
      stream << "int" << bits;
    else if (type.isFloat()) {
      switch (bits) {
        case 32:
          stream << "float"; break;
        case 64:
          stream << "double"; break;
        default:
          stream << "float" << bits; break;
      }
    }
    
    stream << "))" << std::endl;
  }

  void emit_symbolic_buffers() {
    for (const auto& item : loadToRegMap)
      emit_symbolic_buffer(item.second, item.first->type, item.first->vector_width);
    
    for (const auto& item : varToRegMap)
      emit_symbolic_buffer(item.second, item.first->type, 1);
  }

  void emit_buffer_id_map() {
    stream << ";; Creating a map between buffers and halide call node arguments" << std::endl
           << "(define " << "id-map" << " (make-hash))" << std::endl;  // map_name

    for (const auto& item : loadToRegMap) {
      stream << "(hash-set! " << "id-map" << " reg_" << item.second << " (bv " << item.second << " (bitvector 8)))" << std::endl;
    }

    for (const auto& item : varToRegMap) {
      stream << "(hash-set! " << "id-map" << " reg_" << item.second << " (bv " << item.second << " (bitvector 8)))" << std::endl;
    }
  }

  void emit_expr(const Expr* op) {
    stream << "(define halide-expr " << std::endl << "\t";
    op->accept(this);  // emit hydride for expression
    stream << std::endl 
           << ")" << std::endl << std::endl
           << "(clear-vc!)" << std::endl;
  }

  void emit_hydride_synthesis(size_t expr_depth, size_t VF) {
    stream << "(define synth-res "
           << "(synthesize-halide-expr "
           << "halide-expr" << " "  // expr_name
           << "id-map" << " "  // id_map_name
           << expr_depth << " "
           << VF << " "
           << "'z3" << " "  // solver
           << "#t" << " "  // optimize
           << "#f" << " "  // symbolic
           << '"' << "" << '"' << " "  // synth_log_path
           << '"' << "" << '"' << " "  // synth_log_name
           << '"' << "x86" << '"' << ")"  // target
           << ")" << std::endl
           << "(dump-synth-res-with-typeinfo synth-res id-map)" << std::endl;
  }

  void emit_compile_to_llvm(std::string benchmark_name, size_t expr_id) {
    stream << ";; Translate synthesized hydride-expression into LLVM-IR" << std::endl
           << "(compile-to-llvm "
           << "synth-res" << " "  // expr_name
           << "id-map" << " "  // map_name
           << '"' << "hydride_node_" << benchmark_name << "_" << expr_id << '"' << " "  // call_name
           << '"' << benchmark_name << '"' << ")" << std::endl;  // bitcode_path
    }

  void emit_write_synth_log_to_file(std::string benchmark_name, size_t expr_id) {
    stream << "(save-synth-map "
           << '"' << "/tmp/hydride_hash_" << benchmark_name << "_" << expr_id << ".rkt" << '"' << " "  // fpath
           << '"' << "synth_hash_" << benchmark_name << "_" << expr_id << '"' << " "  // hash_name
           << "synth-log)";
  }

  void printBinaryOp(Expr a, Expr b, string bv_name) {
    stream << "(" << "vec" << "-" << bv_name << " ";
    a.accept(this);
    stream << " ";
    b.accept(this);
    stream << ")";
  }

  void visit(const Literal* op) {
    stream << "(xBroadcast ";  // broadcast op to vector width

    switch (op->type.getKind()) {
      case Datatype::Bool: {
        stream << "(int-imm (bv " << static_cast<uint16_t>(op->getValue<bool>()) << " 2) #f)";
      } break;
      case Datatype::UInt8: {
        stream << "(int-imm (bv " << static_cast<uint16_t>(op->getValue<uint8_t>()) << " 8) #f)";
      } break;
      case Datatype::UInt16: {
        stream << "(int-imm (bv " << op->getValue<uint16_t>() << " 16) #f)";
      } break;
      case Datatype::UInt32: {
        stream << "(int-imm (bv " << op->getValue<uint32_t>() << " 32) #f)";
      } break;
      case Datatype::UInt64: {
        stream << "(int-imm (bv " << op->getValue<uint64_t>() << " 64) #f)";
      } break;
      case Datatype::UInt128:
        taco_not_supported_yet; break;
      case Datatype::Int8: {
        stream << "(int-imm (bv " << static_cast<int16_t>(op->getValue<int8_t>()) << " 8) #t)";
      } break;
      case Datatype::Int16: {
        stream << "(int-imm (bv " << op->getValue<int16_t>() << " 16) #t)";
      } break;
      case Datatype::Int32: {
        stream << "(int-imm (bv " << op->getValue<int32_t>() << " 32) #t)";
      } break;
      case Datatype::Int64: {
        stream << "(int-imm (bv " << op->getValue<int64_t>() << " 64) #t)";
      } break;
      case Datatype::Int128: 
        taco_ierror << "No support for int128_t types"; break;
      case Datatype::Float32:
        taco_ierror << "No support for float types"; break;
      case Datatype::Float64:
        taco_ierror << "No support for float types"; break;
      case Datatype::Complex64:
      case Datatype::Complex128:
        taco_ierror << "No support for complex numbers"; break;
      case Datatype::Undefined:
        taco_ierror << "Undefined type in IR"; break;
    }

    stream << " " << vector_width << ")";
  }

  void visit(const Var* op) {
    // For Vars, we replace their names with the generated name, since we match by reference (not name)
    // taco_iassert(varMap.count(op) > 0) << "Var " << op->name << " not found in varMap";

    stream << "(xBroadcast ";  // broadcast var to vector width

    if (varToRegMap.find(op) != varToRegMap.end())
      stream << "reg_" << varToRegMap[op];
    else
      taco_ierror << "Var node not found in varToRegMap.";

    stream << " " << vector_width << ")";
  }

  void visit(const Neg* op) {
    stream << "(<neg> ";
    op->a.accept(this);
    stream << ")";
  }

  void visit(const Sqrt* op) {
    taco_tassert(op->type.isFloat() && op->type.getNumBits() == 64) <<
        "Codegen doesn't currently support non-double sqrt";  
    stream << "(<sqrt> ";
    op->a.accept(this);
    stream << ")";
  }

  void visit(const Add* op) { printBinaryOp(op->a, op->b, "add"); }

  void visit(const Sub* op) { printBinaryOp(op->a, op->b, "sub"); }

  void visit(const Mul* op) { printBinaryOp(op->a, op->b, "mul"); }

  void visit(const Div* op) { printBinaryOp(op->a, op->b, "div"); }

  void visit(const Rem* op) { printBinaryOp(op->a, op->b, "mod"); }

  void visit(const Min* op) {
    if (op->operands.size() == 1) {
      op->operands[0].accept(this);
    } else {
      for (size_t i = 0; i < op->operands.size() - 1; ++i) {
        stream << "(vec-min ";
        op->operands[i].accept(this);
        stream << " ";
      }
      op->operands.back().accept(this);
      for (size_t i = 0; i < op->operands.size() - 1; ++i) {
        stream << ")";
      }
    }
  }

  void visit(const Max* op) {
    if (op->operands.size() == 1) {
      op->operands[0].accept(this);
    } else {
      for (size_t i = 0; i < op->operands.size() - 1; ++i) {
        stream << "(vec-max ";
        op->operands[i].accept(this);
        stream << " ";
      }
      op->operands.back().accept(this);
      for (size_t i = 0; i < op->operands.size() - 1; ++i) {
        stream << ")";
      }
    }
  }

  void visit(const BitAnd* op) { printBinaryOp(op->a, op->b, "bwand"); }

  // todo: might not be supported in hydride
  void visit(const BitOr* op) { printBinaryOp(op->a, op->b, "bwor"); }

  void visit(const Eq* op) { printBinaryOp(op->a, op->b, "eq"); }

  void visit(const Neq* op) { printBinaryOp(op->a, op->b, "ne"); }

  void visit(const Gt* op) { printBinaryOp(op->a, op->b, "gt"); }

  void visit(const Lt* op) { printBinaryOp(op->a, op->b, "lt"); }

  void visit(const Gte* op) { printBinaryOp(op->a, op->b, "ge"); }

  void visit(const Lte* op) { printBinaryOp(op->a, op->b, "le"); }

  // todo: NOT SURE IF THIS IS CORRECT??? 
  void visit(const And* op) { printBinaryOp(op->a, op->b, "and"); }

  // todo: NOT SURE IF THIS IS CORRECT??? 
  void visit(const Or* op) { printBinaryOp(op->a, op->b, "or"); }

  void visit(const BinOp* op) {
    stream << "(<binop> ";
    op->a.accept(this);
    stream << " ";
    op->b.accept(this);
    stream << ")";
  }

  void visit(const Cast* op) {
    stream << "(<cast> ";
    op->a.accept(this);
    stream << ")";
  }

  void visit(const Call* op) {
    stream << "(<call> ";
    for (auto& arg : op->args) {
      stream << " ";
      arg.accept(this);
    }
    stream << ")";
  }

  void visit(const Load* op) {
    if (op->vector_width > 1) {
      stream << "(xBroadcast ";  // broadcast op to vector width
    }

    if (loadToRegMap.find(op) != loadToRegMap.end())
      stream << "reg_" << loadToRegMap[op];
    else
      taco_ierror << "Load node not found in loadToRegMap.";

    if (op->vector_width > 1) {
      stream << " " << vector_width << ")";
    }
  }

  void visit(const Malloc* op) {
    taco_ierror << "Malloc node not supported in hydride translation.";
  }

  void visit(const Sizeof* op) {
    taco_ierror << "Sizeof node not supported in hydride translation.";
  }

  void visit(const GetProperty* op) {
    taco_ierror << "GetProperty node not supported in hydride translation.";
  }
};

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
  }

  void emit_llvm_function(const Store* op, std::string name, const std::vector<Expr>& args) {
    stream << "define void @shim_" << name << "(";
    
    // emit destination argument
    std::string dest_type = get_type(Load::make(op->arr, op->loc, op->vector_width));
    stream << dest_type << "* dst";

    // emit other arguments
    for (size_t i = 0; i < args.size(); ++i)
      stream << ", " << get_type(args[i]) << (isa<Load>(args[i])? "*": "") << " reg_" << i;

    stream << ") #0 {" << std::endl << std::endl;

    stream << '\t' << "; load dst" << std::endl
           << '\t' << "%gep_dst = getelementptr " << dest_type << ", " << dest_type << "* %dst, " << dest_type << " %0" << std::endl
           << '\t' << "%vect_dst_ptr = bitcast " << dest_type << "* %gep_dst to <" << op->vector_width << " x " << dest_type << ">*" << std::endl
           << std::endl;

    for (size_t i = 0; i < args.size(); ++i) {
      if (isa<Load>(args[i])) {
        const Load* arg = args[i].as<Load>();
        std::string arg_type = get_type(arg);
        stream << '\t' << "; load reg_" << i << std::endl
               << '\t' << "%gep_reg_" << i << " = getelementptr " << arg_type << ", " << arg_type << "* %reg_" << i << ", " << arg_type << " %0" << std::endl
               << '\t' << "%vect_reg_" << i << "_ptr = bitcast " << arg_type << "* %gep_reg_" << i << " to <" << arg->vector_width << " x " << arg_type << ">*" << std::endl
               << '\t' << "%vect_reg_" << i << " = load <" << arg->vector_width << " x " << arg_type << ">,<" << arg->vector_width << " x " << arg_type << ">* %vect_reg_" << i << "_ptr" << std::endl
               << std::endl;
      }
    }

    stream << '\t' << "; extern call to hydride generated function" << std::endl
           << '\t' << "%ret_val = " << name << " <" << op->vector_width << " x " << dest_type << "> %vect_dst";
    for (size_t i = 0; i < args.size(); ++i)
      stream << ", %reg_" << i;
    stream << std::endl << std::endl;

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

class ExprOptimizer : public IRRewriter {
  // Visits a Taco loop body and optimizes each expression.

 public:
  ExprOptimizer(std::string benchmark_name) : benchmark_name(benchmark_name) {}

 protected:
  using IRRewriter::visit;
  std::string benchmark_name;
  size_t expr_count = 0;

  // Helper function for all valid expressions
  Expr synthExpr(Expr op) {
    /** 
     * Mutation steps:
     * 
     * If the expression produces an output of float type, ignore it
     * If the expression produces an output of boolean type, ignore it
     * Ignore some qualifying but trivial expressions to reduce noise in the results
     */

    // 1. Generate the hydride expression and write to a racket file.
    size_t expr_id = expr_count++;
    std::string file_name = "taco_expr_" + benchmark_name + "_" + std::to_string(expr_id) + ".rkt";

    std::ofstream ostream;
    ostream.open(file_name);
    HydrideEmitter hydride_emitter(ostream);
    // todo: calculate vector width
    hydride_emitter.translate(&op, benchmark_name, expr_id, /* vector_width */ 4);
    ostream.close();
    std::cout << "Wrote racket code to file @ " << file_name << std::endl;

    // 2. Actually synthesize the expression with Hydride.
    // std::string cmd = "racket " + file_name;

    // auto start = std::chrono::system_clock::now();
    // int ret_code = system(cmd.c_str());
    // auto end = std::chrono::system_clock::now();
    // taco_iassert(ret_code == 0) << "Synthesis crashed, exiting ...";
    // std::cout << "Synthesis took " << (end - start).count() << "seconds ..." << "\n";

    // 3. Replace the expression with an external llvm function call.
    // todo: replace function call to shim!!!
    std::string function_name = "hydride_node_" + benchmark_name + "_" + std::to_string(expr_id);
    std::vector<Expr> args(hydride_emitter.loadToRegMap.size() + hydride_emitter.varToRegMap.size());
    
    for (const auto& item : hydride_emitter.loadToRegMap) {
      args[item.second] = item.first;
    }

    for (const auto& item : hydride_emitter.varToRegMap) {
      args[item.second] = item.first;
    }

    return Call::make(function_name, args, op.type(), /* extern_llvm */ true);
  }

  void visit(const Neg* op) override { expr = synthExpr(op); }
  
  void visit(const Sqrt* op) override { expr = synthExpr(op); }
  
  void visit(const Add* op) override { expr = synthExpr(op); }
  
  void visit(const Sub* op) override { expr = synthExpr(op); }
  
  void visit(const Mul* op) override { expr = synthExpr(op); }
  
  void visit(const Div* op) override { expr = synthExpr(op); }
  
  void visit(const Rem* op) override { expr = synthExpr(op); }
  
  void visit(const Min* op) override { expr = synthExpr(op); }
  
  void visit(const Max* op) override { expr = synthExpr(op); }
  
  void visit(const BitAnd* op) override { expr = synthExpr(op); }
  
  void visit(const BitOr* op) override { expr = synthExpr(op); }
  
  void visit(const Eq* op) override { expr = synthExpr(op); }
  
  void visit(const Neq* op) override { expr = synthExpr(op); }
  
  void visit(const Gt* op) override { expr = synthExpr(op); }
  
  void visit(const Lt* op) override { expr = synthExpr(op); }
  
  void visit(const Gte* op) override { expr = synthExpr(op); }
  
  void visit(const Lte* op) override { expr = synthExpr(op); }
  
  void visit(const And* op) override { expr = synthExpr(op); }
  
  void visit(const Or* op) override { expr = synthExpr(op); }
  
  void visit(const BinOp* op) override { expr = synthExpr(op); }
  
  void visit(const Cast* op) override { expr = synthExpr(op); }

};

class LoopOptimizer : public IRRewriter {
  // Visits a Taco IR function and identifies the candidates for synthesis.
 public:
  LoopOptimizer(std::string benchmark_name) : expr_optimizer(benchmark_name) {}

 protected:
  using IRRewriter::visit;
  ExprOptimizer expr_optimizer;
  size_t in_vectorizable_loop;

  class LoopDetector : public IRVisitor {
    // Visits a Taco IR for loop and returns whether there is a inner loop.
   public:
    LoopDetector() : found(false) {};

    bool visit(const Stmt& op) {
      op.accept(this);
      return found;
    }

   protected:
    using IRVisitor::visit;
    bool found;

    void visit(const For* op) { found = true; }
    void visit(const While* op) { found = true; }
  };

  LoopDetector loop_detector;

  void visit(const For* op) {
    // If this is not the innermost loop, keep traversing
    if (loop_detector.visit(op->contents))
      return IRRewriter::visit(op);

    // Check if the loop is vectorizable??? todo: implement
    in_vectorizable_loop++;
    IRRewriter::visit(op);
    // Rewrite the for loop bounds if necessary
    in_vectorizable_loop--;
  }

  void visit(const Store* op) {
    if (!in_vectorizable_loop)
      return IRRewriter::visit(op);

    stmt = Store::make(op->arr, op->loc, expr_optimizer.rewrite(op->data),
                       op->use_atomics, op->atomic_parallel_unit);
  }
};

} // anonymous namespace

Stmt optimize_instructions_synthesis(Stmt stmt) {
  std::cout << "ס" << std::endl;

  std::string benchmark_name = "hydride";

  // Run the optimizer that targets the innermost vectorizable loop.
  stmt = LoopOptimizer(benchmark_name).rewrite(stmt);

  // Emit llvm shim to the hydride generated .ll files.
  std::string file_name = "llvm_shim_" + benchmark_name + ".ll";
  std::ofstream ostream;
  ostream.open(file_name);
  LLVMShimEmitter(ostream, file_name).visit(&stmt);
  ostream.close();
  std::cout << "Wrote llvm shim to file @ " << file_name << std::endl;

  return stmt;

    // find all expressions we want to synth
    // output rosette code for each
    // replace with external function call .ll

  // run synthesis on each
  // output the .ll files
  // run c code (somewhere else)
}

}
}
