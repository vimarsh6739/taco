#include <iostream>
#include <fstream>
#include <dlfcn.h>
#include <algorithm>
#include <unordered_set>
#include <taco.h>

#include "taco/ir/ir_printer.h"
#include "taco/ir/ir_visitor.h"
#include "codegen_hydride.h"
#include "taco/error.h"
#include "taco/util/strings.h"
#include "taco/util/collections.h"

using namespace std;

namespace taco {
namespace ir {

// Some helper functions
namespace {

} // anonymous namespace

// find variables for generating declarations
// generates a single var for each GetProperty
class CodeGen_Hydride::FindVars : public IRVisitor {
public:
  map<Expr, string, ExprCompare> varMap;

  // the variables for which we need to add declarations
  map<Expr, string, ExprCompare> varDecls;

  vector<Expr> localVars;

  // this maps from tensor, property, mode, index to the unique var
  map<tuple<Expr, TensorProperty, int, int>, string> canonicalPropertyVar;

  // this is for convenience, recording just the properties unpacked
  // from the output tensor so we can re-save them at the end
  map<tuple<Expr, TensorProperty, int, int>, string> outputProperties;

  // TODO: should replace this with an unordered set
  vector<Expr> outputTensors;
  vector<Expr> inputTensors;

  CodeGen_Hydride *codeGen;

  // copy inputs and outputs into the map
  FindVars(vector<Expr> inputs, vector<Expr> outputs, CodeGen_Hydride *codeGen)
  : codeGen(codeGen) {
    for (auto v: inputs) {
      auto var = v.as<Var>();
      taco_iassert(var) << "Inputs must be vars in codegen";
      taco_iassert(varMap.count(var)==0) << "Duplicate input found in codegen";
      inputTensors.push_back(v);
      varMap[var] = var->name;
    }
    for (auto v: outputs) {
      auto var = v.as<Var>();
      taco_iassert(var) << "Outputs must be vars in codegen";
      taco_iassert(varMap.count(var)==0) << "Duplicate output found in codegen";
      outputTensors.push_back(v);
      varMap[var] = var->name;
    }
  }

protected:
  using IRVisitor::visit;

  virtual void visit(const Var *op) {
    if (varMap.count(op) == 0) {
      varMap[op] = op->is_ptr? op->name : codeGen->genUniqueName(op->name);
    }
  }

  virtual void visit(const VarDecl *op) {
    if (!util::contains(localVars, op->var)) {
      localVars.push_back(op->var);
    }
    op->var.accept(this);
    op->rhs.accept(this);
  }

  virtual void visit(const For *op) {
    if (!util::contains(localVars, op->var)) {
      localVars.push_back(op->var);
    }
    op->var.accept(this);
    op->start.accept(this);
    op->end.accept(this);
    op->increment.accept(this);
    op->contents.accept(this);
  }

  virtual void visit(const GetProperty *op) {
    if (!util::contains(inputTensors, op->tensor) &&
        !util::contains(outputTensors, op->tensor)) {
      // Don't create header unpacking code for temporaries
      return;
    }

    if (varMap.count(op) == 0) {
      auto key =
              tuple<Expr,TensorProperty,int,int>(op->tensor,op->property,
                                                 (size_t)op->mode,
                                                 (size_t)op->index);
      if (canonicalPropertyVar.count(key) > 0) {
        varMap[op] = canonicalPropertyVar[key];
      } else {
        auto unique_name = codeGen->genUniqueName(op->name);
        canonicalPropertyVar[key] = unique_name;
        varMap[op] = unique_name;
        varDecls[op] = unique_name;
        if (util::contains(outputTensors, op->tensor)) {
          outputProperties[key] = unique_name;
        }
      }
    }
  }
};

CodeGen_Hydride::CodeGen_Hydride(std::ostream &dest)
    : CodeGen(dest, false, true, C), out(dest), outputKind(ImplementationGen) {}

CodeGen_Hydride::~CodeGen_Hydride() {}

// ORIGINAL METHOD:
void CodeGen_Hydride::compile(Stmt stmt, bool isFirst) {
  varMap = {};
  localVars = {};
  out << endl;
  // generate code for the Stmt
  stmt.accept(this);
}

// DUMMY METHOD:
// void CodeGen_Hydride::compile(Stmt stmt, bool isFirst) {
//   out << "HELLO THIS IS A DUMDUM!!" << endl;

//   // call IR Printer
//   IRPrinter printer(out);
//   stmt.accept(&printer);
// }

// Helper functions

void CodeGen_Hydride::printBinaryOp(Expr a, Expr b, string bv_name) {
  stream << "(" << "vec" << "-" << bv_name << " ";
  a.accept(this);
  stream << " ";
  b.accept(this);
  stream << ")";
}

void CodeGen_Hydride::visit(const Literal* op) {
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

// For Vars, we replace their names with the generated name,
// since we match by reference (not name)
void CodeGen_Hydride::visit(const Var* op) {
  taco_iassert(varMap.count(op) > 0) << "Var " << op->name << " not found in varMap";
  stream << "(<var> " << op->type << " " << varMap[op] << ")";
}

void CodeGen_Hydride::visit(const Neg* op) {
  stream << "/* CodeGenHydride Neg */";
  IRPrinter::visit(op);
}

void CodeGen_Hydride::visit(const Sqrt* op) {
  taco_tassert(op->type.isFloat() && op->type.getNumBits() == 64) <<
      "Codegen doesn't currently support non-double sqrt";  
  stream << "(<sqrt> ";
  op->a.accept(this);
  stream << ")";
}

void CodeGen_Hydride::visit(const Add* op) {
  printBinaryOp(op->a, op->b, "add");
}

void CodeGen_Hydride::visit(const Sub* op) {
  printBinaryOp(op->a, op->b, "sub");
}

void CodeGen_Hydride::visit(const Mul* op) {
  printBinaryOp(op->a, op->b, "mul");
}

void CodeGen_Hydride::visit(const Div* op) {
  printBinaryOp(op->a, op->b, "div");
}

void CodeGen_Hydride::visit(const Rem* op) {
  printBinaryOp(op->a, op->b, "mod");
}

void CodeGen_Hydride::visit(const Min* op) {
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

void CodeGen_Hydride::visit(const Max* op) {
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

void CodeGen_Hydride::visit(const BitAnd* op) {
  printBinaryOp(op->a, op->b, "bwand");
}

void CodeGen_Hydride::visit(const BitOr* op) {
  // todo: might not be supported in hydride
  printBinaryOp(op->a, op->b, "bwor");
}

void CodeGen_Hydride::visit(const Eq* op) {
  printBinaryOp(op->a, op->b, "eq");
}

void CodeGen_Hydride::visit(const Neq* op) {
  printBinaryOp(op->a, op->b, "ne");
}

void CodeGen_Hydride::visit(const Gt* op) {
  printBinaryOp(op->a, op->b, "gt");
}

void CodeGen_Hydride::visit(const Lt* op) {
  printBinaryOp(op->a, op->b, "lt");
}

void CodeGen_Hydride::visit(const Gte* op) {
  printBinaryOp(op->a, op->b, "ge");
}

void CodeGen_Hydride::visit(const Lte* op) {
  printBinaryOp(op->a, op->b, "le");
}

void CodeGen_Hydride::visit(const And* op) {
  // todo: NOT SURE IF THIS IS CORRECT???
  printBinaryOp(op->a, op->b, "and");
}

void CodeGen_Hydride::visit(const Or* op) {
  // todo: NOT SURE IF THIS IS CORRECT???
  printBinaryOp(op->a, op->b, "or");
}

void CodeGen_Hydride::visit(const BinOp* op) {
  stream << "/* CodeGenHydride BinOp */";
  IRPrinter::visit(op);
}

void CodeGen_Hydride::visit(const Cast* op) {
  stream << "/* CodeGenHydride Cast */";
  IRPrinter::visit(op);
}

void CodeGen_Hydride::visit(const Call* op) {
  stream << "/* CodeGenHydride Call */";
  IRPrinter::visit(op);
}

void CodeGen_Hydride::visit(const Load* op) {
  stream << "(<load> ";
  op->arr.accept(this);
  stream << " ";
  op->loc.accept(this);
  stream << ")";
}

void CodeGen_Hydride::visit(const Malloc* op) {
  stream << "(<malloc> ";
  op->size.accept(this);
  stream << ")";
}

void CodeGen_Hydride::visit(const Sizeof* op) {
  stream << "(<sizeof> ";
  stream << op->sizeofType;
  stream << ")";
}

void CodeGen_Hydride::visit(const GetProperty* op) {
  stream << "(<prop> ";
  taco_iassert(varMap.count(op) > 0) << "Property " << Expr(op) << " of " << op->tensor << " not found in varMap";
  out << varMap[op];
  stream << ")";
}

void CodeGen_Hydride::visit(const Block* op) {
  stream << "(<block>";
  for (size_t i = 0; i < op->contents.size(); ++i) {
    stream << " ";
    op->contents[i].accept(this);
  }
  stream << ")";
}

void CodeGen_Hydride::visit(const Scope* op) {
  stream << "(<scope> ";
  varNames.scope();
  op->scopedStmt.accept(this);
  varNames.unscope();
  stream << ")";
}

void CodeGen_Hydride::visit(const IfThenElse* op) {
  stream << "/* CodeGenHydride IfThenElse */";
  IRPrinter::visit(op);
}

void CodeGen_Hydride::visit(const Case* op) {
  stream << "/* CodeGenHydride Case */";
  IRPrinter::visit(op);
}

void CodeGen_Hydride::visit(const Switch* op) {
  stream << "/* CodeGenHydride Switch */";
  IRPrinter::visit(op);
}

void CodeGen_Hydride::visit(const Store* op) {
  stream << "(<store> ";
  op->arr.accept(this);
  stream << " ";
  op->loc.accept(this);
  stream << " ";
  op->data.accept(this);
  stream << ")";
}

void CodeGen_Hydride::visit(const For* op) {
  stream << "(<for> "; 
  op->var.accept(this);
  stream << " ";
  op->start.accept(this);
  stream << " ";
  op->end.accept(this);
  stream << " ";
  op->increment.accept(this);
  stream << " ";
  op->contents.accept(this);
  stream << ")";
}

void CodeGen_Hydride::visit(const While* op) {
  stream << "/* CodeGenHydride While */";
  IRPrinter::visit(op);
}

void CodeGen_Hydride::visit(const Function* func) {
  stream << "/* CodeGenHydride Function */";
  // if generating a header, protect the function declaration with a guard
  if (outputKind == HeaderGen) {
    out << "#ifndef TACO_GENERATED_" << func->name << "\n";
    out << "#define TACO_GENERATED_" << func->name << "\n";
  }

  int numYields = countYields(func);
  emittingCoroutine = (numYields > 0);
  funcName = func->name;
  labelCount = 0;

  resetUniqueNameCounters();
  FindVars inputVarFinder(func->inputs, {}, this);
  func->body.accept(&inputVarFinder);
  FindVars outputVarFinder({}, func->outputs, this);
  func->body.accept(&outputVarFinder);

  // output function declaration
  doIndent();
  out << printFuncName(func, inputVarFinder.varDecls, outputVarFinder.varDecls);

  // if we're just generating a header, this is all we need to do
  if (outputKind == HeaderGen) {
    out << ";\n";
    out << "#endif\n";
    return;
  }

  out << " {\n";

  indent++;

  // find all the vars that are not inputs or outputs and declare them
  resetUniqueNameCounters();
  FindVars varFinder(func->inputs, func->outputs, this);
  func->body.accept(&varFinder);
  varMap = varFinder.varMap;
  localVars = varFinder.localVars;

  // Print variable declarations
  out << printDecls(varFinder.varDecls, func->inputs, func->outputs) << endl;

  if (emittingCoroutine) {
    out << printContextDeclAndInit(varMap, localVars, numYields, func->name)
        << endl;
  }

  // output body
  print(func->body);

  // output repack only if we allocated memory
  if (checkForAlloc(func))
    out << endl << printPack(varFinder.outputProperties, func->outputs);

  if (emittingCoroutine) {
    out << printCoroutineFinish(numYields, funcName);
  }

  doIndent();
  out << "return 0;\n";
  indent--;

  doIndent();
  out << "}\n";
}

void CodeGen_Hydride::visit(const VarDecl* op) {
  stream << "/* CodeGenHydride VarDecl */";
  if (emittingCoroutine) {
    doIndent();
    op->var.accept(this);
    parentPrecedence = Precedence::TOP;
    stream << " = ";
    op->rhs.accept(this);
    stream << ";";
    stream << endl;
  } else {
    IRPrinter::visit(op);
  }
}

void CodeGen_Hydride::visit(const Assign* op) {
  stream << "/* CodeGenHydride Assign */";
  IRPrinter::visit(op);
}

void CodeGen_Hydride::visit(const Yield* op) {
  stream << "/* CodeGenHydride Yield */";
  printYield(op, localVars, varMap, labelCount, funcName);
}

void CodeGen_Hydride::visit(const Allocate* op) {
  stream << "/* CodeGenHydride Allocate */";
  string elementType = printCType(op->var.type(), false);

  doIndent();
  op->var.accept(this);
  stream << " = (";
  stream << elementType << "*";
  stream << ")";
  if (op->is_realloc) {
    stream << "realloc(";
    op->var.accept(this);
    stream << ", ";
  }
  else {
    // If the allocation was requested to clear the allocated memory,
    // use calloc instead of malloc.
    if (op->clear) {
      stream << "calloc(1, ";
    } else {
      stream << "malloc(";
    }
  }
  stream << "sizeof(" << elementType << ")";
  stream << " * ";
  parentPrecedence = MUL;
  op->num_elements.accept(this);
  parentPrecedence = TOP;
  stream << ");";
    stream << endl;
}

void CodeGen_Hydride::visit(const Free* op) {
  stream << "/* CodeGenHydride Free */";
  IRPrinter::visit(op);
}

void CodeGen_Hydride::visit(const Comment* op) {
  stream << "/* CodeGenHydride Comment */";
  IRPrinter::visit(op);
}

void CodeGen_Hydride::visit(const BlankLine* op) {
  stream << endl;
}

void CodeGen_Hydride::visit(const Continue* op) {
  stream << "/* CodeGenHydride Continue */";
  IRPrinter::visit(op);
}

void CodeGen_Hydride::visit(const Print* op) {
  stream << "/* CodeGenHydride Print */";
  IRPrinter::visit(op);
}

void CodeGen_Hydride::visit(const Sort* op) {
  stream << "/* CodeGenHydride Sort */";
  IRPrinter::visit(op);
}

void CodeGen_Hydride::visit(const Break* op) {
  stream << "/* CodeGenHydride Break */";
  IRPrinter::visit(op);
}

void CodeGen_Hydride::generateShim(const Stmt& func, stringstream &ret) {
  const Function *funcPtr = func.as<Function>();

  ret << "int _shim_" << funcPtr->name << "(void** parameterPack) {\n";
  ret << "  return " << funcPtr->name << "(";

  size_t i=0;
  string delimiter = "";

  const auto returnType = funcPtr->getReturnType();
  if (returnType.second != Datatype()) {
    ret << "(void**)(parameterPack[0]), ";
    ret << "(char*)(parameterPack[1]), ";
    ret << "(" << returnType.second << "*)(parameterPack[2]), ";
    ret << "(int32_t*)(parameterPack[3])";

    i = 4;
    delimiter = ", ";
  }

  for (auto output : funcPtr->outputs) {
    auto var = output.as<Var>();
    auto cast_type = var->is_tensor ? "taco_tensor_t*"
    : printCType(var->type, var->is_ptr);

    ret << delimiter << "(" << cast_type << ")(parameterPack[" << i++ << "])";
    delimiter = ", ";
  }
  for (auto input : funcPtr->inputs) {
    auto var = input.as<Var>();
    auto cast_type = var->is_tensor ? "taco_tensor_t*"
    : printCType(var->type, var->is_ptr);
    ret << delimiter << "(" << cast_type << ")(parameterPack[" << i++ << "])";
    delimiter = ", ";
  }
  ret << ");\n";
  ret << "}\n";
}
}
}
