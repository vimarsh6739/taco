#include <iostream>
#include <fstream>
#include <dlfcn.h>
#include <algorithm>
#include <unordered_set>
#include <taco.h>

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
  HydrideEmitter(std::ostream& stream) : stream(stream) {};

  void translate(const Expr* op, std::string benchmark_name, size_t expr_id, size_t vector_width) {
    bitwidth = 512;

    emit_racket_imports();
    stream << std::endl;
    emit_racket_debug();
    stream << std::endl;
    emit_set_current_bitwidth();
    stream << std::endl;
    emit_set_memory_limit(20000);
    stream << std::endl;

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

  std::map<Expr, std::string, ExprCompare> varMap;
  std::vector<Expr> localVars;

  // map from taco load instructions to racket registers
  std::map<const Load*, uint> loadToRegMap;

  // map from taco variables to racket registers
  std::map<const Var*, uint> varToRegMap;

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
    stream << "(custodian-limit-memory (current-custodian) (* " << mb << " 1024 1024))";
  }

  void emit_symbolic_buffer(uint reg_num, Datatype type) {
    std::string reg_name = "reg_" + std::to_string(reg_num);
    // size_t bitwidth = item.first->type.bits() * item.first->type.lanes();

    stream << "(define " << reg_name << "_bitvector (bv 0 (bitvector " << bitwidth << ")))" << std::endl
           << "(define " << reg_name << " (halide:create-buffer " << reg_name << "_bitvector '";
    
    int bits = type.getNumBits();
    if (type.isUInt())
      stream << "uint" << bits;
    if (type.isInt())
      stream << "int" << bits;
    if (type.isFloat()) {
      switch (bits) {
        case 32:
          stream << "float";
        case 64:
          stream << "double";  
        default:
          stream << "float" << bits;
      }
    }
    
    stream << "))" << std::endl;
  }

  void emit_symbolic_buffers() {
    for (const auto& item : loadToRegMap)
      emit_symbolic_buffer(item.second, item.first->type);
    
    for (const auto& item : varToRegMap)
      emit_symbolic_buffer(item.second, item.first->type);
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
    switch (op->type.getKind()) {
      case Datatype::Bool: {
        stream << "(<literal> " << op->type << " " << op->getValue<bool>() << ")"; break;
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
        taco_not_supported_yet; break;
      case Datatype::Float32:
        stream << "(<literal> " << op->type << " " << ((op->getValue<float>() != 0.0) ? util::toString(op->getValue<float>()) : "0.0") << ")"; break;
      case Datatype::Float64:
        stream << "(<literal> " << op->type << " " << ((op->getValue<double>()!=0.0) ? util::toString(op->getValue<double>()) : "0.0") << ")"; break;
      case Datatype::Complex64:
      case Datatype::Complex128:
        taco_ierror << "No support for complex numbers"; break;
      case Datatype::Undefined:
        taco_ierror << "Undefined type in IR"; break;
    }
  }

  void visit(const Var* op) {
    // if op is skipnode, emit nothing

    // For Vars, we replace their names with the generated name, since we match by reference (not name)
    // taco_iassert(varMap.count(op) > 0) << "Var " << op->name << " not found in varMap";

    if (varToRegMap.find(op) != varToRegMap.end()) {
      stream << "reg_" << varToRegMap[op];
    } else {
      size_t reg_counter = varToRegMap.size() + loadToRegMap.size();
      varToRegMap[op] = reg_counter;
      stream << "reg_" << reg_counter;
    }
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

  void visit(const Add* op) {
    printBinaryOp(op->a, op->b, "add");
  }

  void visit(const Sub* op) {
    printBinaryOp(op->a, op->b, "sub");
  }

  void visit(const Mul* op) {
    printBinaryOp(op->a, op->b, "mul");
  }

  void visit(const Div* op) {
    printBinaryOp(op->a, op->b, "div");
  }

  void visit(const Rem* op) {
    printBinaryOp(op->a, op->b, "mod");
  }

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

  void visit(const BitAnd* op) {
    printBinaryOp(op->a, op->b, "bwand");
  }

  void visit(const BitOr* op) {
    // todo: might not be supported in hydride
    printBinaryOp(op->a, op->b, "bwor");
  }

  void visit(const Eq* op) {
    printBinaryOp(op->a, op->b, "eq");
  }

  void visit(const Neq* op) {
    printBinaryOp(op->a, op->b, "ne");
  }

  void visit(const Gt* op) {
    printBinaryOp(op->a, op->b, "gt");
  }

  void visit(const Lt* op) {
    printBinaryOp(op->a, op->b, "lt");
  }

  void visit(const Gte* op) {
    printBinaryOp(op->a, op->b, "ge");
  }

  void visit(const Lte* op) {
    printBinaryOp(op->a, op->b, "le");
  }

  void visit(const And* op) {
    // todo: NOT SURE IF THIS IS CORRECT???
    printBinaryOp(op->a, op->b, "and");
  }

  void visit(const Or* op) {
    // todo: NOT SURE IF THIS IS CORRECT???
    printBinaryOp(op->a, op->b, "or");
  }

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
    // insert skipnodes for predicate and index???
    

    stream << "(<load> ";
    op->arr.accept(this);
    stream << " ";
    op->loc.accept(this);
    stream << ")";
  }

  void visit(const Malloc* op) {
    stream << "(<malloc> ";
    op->size.accept(this);
    stream << ")";
  }

  void visit(const Sizeof* op) {
    stream << "(<sizeof> ";
    stream << op->sizeofType;
    stream << ")";
  }

  void visit(const GetProperty* op) {
    stream << "(<prop> ";
    // taco_iassert(varMap.count(op) > 0) << "Property " << Expr(op) << " of " << op->tensor << " not found in varMap";
    // stream << varMap[op];
    stream << ")";
  }
};

class LoadRewriter : public IRRewriter {
  // store vector of temp variables to load expressions
  uint tempVarCount = 0;
  vector<pair<string, const Expr>> decls;

 protected:
    using IRRewriter::visit;

  void visit(const Load* op) override {
    IRRewriter::visit(op);
    string varName = "temp_load_" + std::to_string(tempVarCount++);
    decls.emplace_back(varName, expr);
    std::cout << "HI: " << expr << std::endl;
    expr = Var::make(varName, expr.type());
    std::cout << "HI: " << expr << std::endl;
  }

  void visit(const Store* op) override {
    IRRewriter::visit(op);
    insertDecls();
  }
  
  void visit(const IfThenElse* op) override {
    IRRewriter::visit(op);
    insertDecls();
  }

  void visit(const Switch* op) override {
    IRRewriter::visit(op);
    insertDecls();
  }

  void visit(const For* op) override {
    IRRewriter::visit(op);
    insertDecls();
  }

  void visit(const While* op) override {
    IRRewriter::visit(op);
    insertDecls();
  }

  void visit(const Assign* op) override {
    IRRewriter::visit(op);
    insertDecls();
  }

  void visit(const Yield* op) override {
    IRRewriter::visit(op);
    insertDecls();
  }

  void visit(const Print* op) override {
    IRRewriter::visit(op);
    insertDecls();
  }

 private:
  void insertDecls() {
    if (decls.size() > 0) {
      vector<Stmt> stmts;
      for (const auto& decl: decls) {
        stmts.push_back(VarDecl::make(Var::make(decl.first, decl.second.type()), decl.second));
      }
      stmts.push_back(stmt);
      stmt = Scope::make(Block::make(stmts));
      decls.clear();
    }
  }

};

class ExprOptimizer : public IRRewriter {
  // Visits a Taco loop body and optimizes each expression.

  // Expr rewrite(Expr e) override {
  //   return e;
  // }

 protected:
  using IRRewriter::visit;
  size_t expr_count = 0;

  // Helper function for all valid expressions
  Expr synthExpr(Expr op) {
    std::cout << "<אקספר>" << op << std::endl;

    /** 
     * Mutation steps:
     * 
     * If the expression produces an output of float type, ignore it
     * If the expression produces an output of boolean type, ignore it
     * Ignore some qualifying but trivial expressions to reduce noise in the results
     */

    std::string benchmark_name = "hydride";
    size_t expr_id = expr_count++;
    std::string file_name = "taco_expr_" + benchmark_name + "_" + std::to_string(expr_id) + ".rkt";

    std::ofstream ostream;
    ostream.open(file_name);
    HydrideEmitter(ostream).translate(&op, benchmark_name, expr_id, /* vector_width */ 1);
    ostream.close();
    std::cout << "Wrote racket code to file @ " << file_name << std::endl;

    return op;
  }

  void visit(const Neg* op) override {
    std::cout << "<Neg>";
    expr = synthExpr(op);
  }
  
  void visit(const Sqrt* op) override {
    std::cout << "<Sqrt>";
    expr = synthExpr(op);
  }
  
  void visit(const Add* op) override {
    std::cout << "<Add>";
    expr = synthExpr(op);
  }
  
  void visit(const Sub* op) override {
    std::cout << "<Sub>";
    expr = synthExpr(op);
  }
  
  void visit(const Mul* op) override {
    std::cout << "<Mul>";
    expr = synthExpr(op);
  }
  
  void visit(const Div* op) override {
    std::cout << "<Div>";
    expr = synthExpr(op);
  }
  
  void visit(const Rem* op) override {
    std::cout << "<Rem>";
    expr = synthExpr(op);
  }
  
  void visit(const Min* op) override {
    std::cout << "<Min>";
    expr = synthExpr(op);
  }
  
  void visit(const Max* op) override {
    std::cout << "<Max>";
    expr = synthExpr(op);
  }
  
  void visit(const BitAnd* op) override {
    std::cout << "<BitAnd>";
    expr = synthExpr(op);
  }
  
  void visit(const BitOr* op) override {
    std::cout << "<BitOr>";
    expr = synthExpr(op);
  }
  
  void visit(const Eq* op) override {
    std::cout << "<Eq>";
    expr = synthExpr(op);
  }
  
  void visit(const Neq* op) override {
    std::cout << "<Neq>";
    expr = synthExpr(op);
  }
  
  void visit(const Gt* op) override {
    std::cout << "<אקספר>";
    expr = synthExpr(op);
  }
  
  void visit(const Lt* op) override {
    std::cout << "<Lt>";
    expr = synthExpr(op);
  }
  
  void visit(const Gte* op) override {
    std::cout << "<Gte>";
    expr = synthExpr(op);
  }
  
  void visit(const Lte* op) override {
    std::cout << "<Lte>";
    expr = synthExpr(op);
  }
  
  void visit(const And* op) override {
    std::cout << "<And>";
    expr = synthExpr(op);
  }
  
  void visit(const Or* op) override {
    std::cout << "<Or>";
    expr = synthExpr(op);
  }
  
  void visit(const BinOp* op) override {
    std::cout << "<BinOp>";
    expr = synthExpr(op);
  }
  
  void visit(const Cast* op) override {
    std::cout << "<Cast>";
    expr = synthExpr(op);
  }

};

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

class LoopOptimizer : public IRRewriter {
  // Visits a Taco IR function and identifies the candidates for synthesis.

 protected:
  using IRRewriter::visit;
  ExprOptimizer expr_optimizer;

  void visit(const For* op) {
    // If this is not the innermost loop, keep traversing
    if (LoopDetector().visit(op->contents))
      return IRRewriter::visit(op);
    
    // Check if the loop is vectorizable??? todo: implement
    IRPrinter(std::cout).print(op->contents);
    expr_optimizer.rewrite(op->contents);
  }
};



} // anonymous namespace

// ORIGINAL METHOD:
// void CodeGen_Hydride::compile(Stmt stmt, bool isFirst) {
//   // varMap = {};
//   // localVars = {};
//   // out << endl;
//   // // generate code for the Stmt
//   // stmt.accept(this);

// }

Stmt optimize_instructions_synthesis(Stmt stmt) {
  std::cout << "ס" << std::endl;
  IRPrinter printer(std::cout);

  // stmt = LoadRewriter().rewrite(stmt);
  // stmt = IROptimizer().rewrite(stmt);

  LoopOptimizer().rewrite(stmt);
  return stmt;

  // IROptimizer().rewrite(stmt);

  // IROptimizer().rewrite()
    // find all expressions we want to synth
    // output rosette code for each
    // replace with external function call .ll

  // run synthesis on each
  // output the .ll files
  // run c code (somewhere else)
}

}
}
