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

// Takes a Taco IR expression and converts it to Rosette syntax
class HydrideEmitter : public IRVisitor {
 public:
  HydrideEmitter(std::stringstream& stream, std::string benchmark_name) : stream(stream), benchmark_name(benchmark_name) {};

  void translate(const Expr* op, size_t expr_id) {
    varMap = {};
    localVars = {};
    // Clear LoadToRegMap, RegToLoadMap, RegToVariableMap, VariableToRegMap

    emit_racket_imports();
    stream << std::endl;
    emit_racket_debug();
    stream << std::endl;
    emit_set_current_bitwidth();
    stream << std::endl;
    emit_set_memory_limit(20000);
    stream << std::endl;
    // rkt << HSE.emit_symbolic_buffers() << "\n"; // uses the maps
    // rkt << HSE.emit_buffer_id_map("id-map") << "\n";

    emit_expr(op);
    stream << std::endl;
    emit_hydride_synthesis(/* expr_depth */ 2, /* VF */ 0xcafebabe);
    stream << std::endl;
    emit_compile_to_llvm(expr_id);
    stream << std::endl;
    emit_write_synth_log_to_file(expr_id);
    stream << std::endl;
  }

 protected:
  using IRVisitor::visit;

  std::stringstream& stream;
  std::string benchmark_name;

  std::map<Expr, std::string, ExprCompare> varMap;
  std::vector<Expr> localVars;

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
    const char* bitwidth = getenv("HL_SYNTH_BW");
    if (bitwidth && stoi(bitwidth) > 0) {
      stream << "(current-bitwidth " << stoi(bitwidth) << ")";
    }
  }

  void emit_set_memory_limit(size_t mb) {
    stream << "(custodian-limit-memory (current-custodian) (* " << mb << " 1024 1024))";
  }

  void emit_expr(const Expr* op) {
    stream << "(define halide-expr " << std::endl;
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

  void emit_compile_to_llvm(size_t expr_id) {
    stream << ";; Translate synthesized hydride-expression into LLVM-IR" << std::endl
           << "(compile-to-llvm "
           << "synth-res" << " "  // expr_name
           << "id-map" << " "  // map_name
           << '"' << "hydride_node_" << benchmark_name << "_" << expr_id << '"' << " "  // call_name
           << '"' << benchmark_name << '"' << ")" << std::endl;  // bitcode_path
    }

  void emit_write_synth_log_to_file(size_t expr_id) {
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
    // For Vars, we replace their names with the generated name, since we match by reference (not name)
    // taco_iassert(varMap.count(op) > 0) << "Var " << op->name << " not found in varMap";
    // stream << "(<var> " << op->type << " " << varMap[op] << ")";
    stream << "(<var> " << op->type << " " << op->name << ")";
  }

  void visit(const Neg* op) {
    stream << "/* CodeGenHydride Neg */";
    IRVisitor::visit(op);
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
    stream << "/* CodeGenHydride Min */";
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
    stream << "/* CodeGenHydride BinOp */";
    IRVisitor::visit(op);
  }

  void visit(const Cast* op) {
    stream << "/* CodeGenHydride Cast */";
    IRVisitor::visit(op);
  }

  void visit(const Call* op) {
    stream << "/* CodeGenHydride Call */";
    IRVisitor::visit(op);
  }

  void visit(const Load* op) {
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
    expr = Var::make(varName, expr.type());
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

class IROptimizer : public IRRewriter {

  // Expr rewrite(Expr e) override {
  //   return e;
  // }

 protected:
  using IRRewriter::visit;

  /** 
   * Mutation steps:
   * 
   * If the expression produces an output of float type, ignore it
   * If the expression produces an output of boolean type, ignore it
   * Ignore some qualifying but trivial expressions to reduce noise in the results
   * If the expression is just a single load instruction, ignore it
   * If the expression is just a single ramp instruction, ignore it
   * If the expression is just a variable, ignore it
   * 
   */

  // Helper function for all valid expressions
  // void rewriteExpr(const ExprNode* op) {
  //   std::cout << "<אקספר>";
  // }
  
  void visit(const Neg* op) override {
    IRRewriter::visit(op);
    std::cout << "<Neg>";
    // rewriteExpr(op);
  }
  
  void visit(const Sqrt* op) override {
    IRRewriter::visit(op);
    std::cout << "<Sqrt>";
  }
  
  void visit(const Add* op) override {
    IRRewriter::visit(op);
    std::cout << "<Add>";
  }
  
  void visit(const Sub* op) override {
    IRRewriter::visit(op);
    std::cout << "<Sub>";
  }
  
  void visit(const Mul* op) override {
    IRRewriter::visit(op);
    std::cout << "<Mul>";
  }
  
  void visit(const Div* op) override {
    IRRewriter::visit(op);
    std::cout << "<Div>";
  }
  
  void visit(const Rem* op) override {
    IRRewriter::visit(op);
    std::cout << "<Rem>";
  }
  
  void visit(const Min* op) override {
    IRRewriter::visit(op);
    std::cout << "<Min>";
  }
  
  void visit(const Max* op) override {
    IRRewriter::visit(op);
    std::cout << "<Max>";
  }
  
  void visit(const BitAnd* op) override {
    IRRewriter::visit(op);
    std::cout << "<BitAnd>";
  }
  
  void visit(const BitOr* op) override {
    IRRewriter::visit(op);
    std::cout << "<BitOr>";
  }
  
  void visit(const Eq* op) override {
    IRRewriter::visit(op);
    std::cout << "<Eq>";
  }
  
  void visit(const Neq* op) override {
    IRRewriter::visit(op);
    std::cout << "<Neq>";
  }
  
  void visit(const Gt* op) override {
    IRRewriter::visit(op);
    std::cout << "<אקספר>";
  }
  
  void visit(const Lt* op) override {
    IRRewriter::visit(op);
    std::cout << "<Lt>";
  }
  
  void visit(const Gte* op) override {
    IRRewriter::visit(op);
    std::cout << "<Gte>";
  }
  
  void visit(const Lte* op) override {
    IRRewriter::visit(op);
    std::cout << "<Lte>";
  }
  
  void visit(const And* op) override {
    IRRewriter::visit(op);
    std::cout << "<And>";
  }
  
  void visit(const Or* op) override {
    IRRewriter::visit(op);
    std::cout << "<Or>";
  }
  
  void visit(const BinOp* op) override {
    IRRewriter::visit(op);
    std::cout << "<BinOp>";
  }
  
  //???
  void visit(const Cast* op) override {
    IRRewriter::visit(op);
    std::cout << "<Cast>";
  }
  
  void visit(const Load* op) override {
    IRRewriter::visit(op);
    std::cout << "<Load>";
  }
  
  void visit(const Malloc* op) override {
    IRRewriter::visit(op);
    std::cout << "<Malloc>";
  }
  
  void visit(const Sizeof* op) override {
    IRRewriter::visit(op);
    std::cout << "<Sizeof>";
  }
  
  void visit(const GetProperty* op) override {
    IRRewriter::visit(op);
    std::cout << "<GetProperty>";
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

  
  std::cout << "REWRITTEN" << endl;
  // printer.print(stmt);


  // stmt = IROptimizer().rewrite(stmt);

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
