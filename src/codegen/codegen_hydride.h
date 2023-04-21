#ifndef TACO_BACKEND_HYDRIDE_H
#define TACO_BACKEND_HYDRIDE_H
#include <map>
#include <vector>

#include "taco/ir/ir.h"
#include "taco/ir/ir_printer.h"
#include "codegen.h"

namespace taco {
namespace ir {

/**
 * 
 * High-level Overview of TACO IR
 * 
 * enum IRNodeType {
 *   Literal,
 *   Var,
 *   Neg,
 *   Sqrt,
 *   Add,
 *   Sub,
 *   Mul,
 *   Div,
 *   Rem,
 *   Min,
 *   Max,
 *   BitAnd,
 *   BitOr,
 *   Not,
 *   Eq,
 *   Neq,
 *   Gt,
 *   Lt,
 *   Gte,
 *   Lte,
 *   And,
 *   Or,
 *   BinOp,
 *   Cast,
 *   Call,
 *   IfThenElse,
 *   Case,
 *   Switch,
 *   Load,
 *   Malloc,
 *   Sizeof,
 *   Store,
 *   For,
 *   While,
 *   Block,
 *   Scope,
 *   Function,
 *   VarDecl,
 *   VarAssign,
 *   Yield,
 *   Allocate,
 *   Free,
 *   Comment,
 *   BlankLine,
 *   Print,
 *   GetProperty,
 *   Continue,
 *   Sort,
 *   Break
 * };
 * 
 * enum TensorProperty {
 *   Order,
 *   Dimension,
 *   ComponentSize,
 *   ModeOrdering,
 *   ModeTypes,
 *   Indices,
 *   Values,
 *   FillValue,
 *   ValuesSize
 * };
 * 
 * enum LoopKind {
 *   Serial, 
 *   Static, 
 *   Dynamic, 
 *   Runtime, 
 *   Vectorized, 
 *   Static_Chunked
 * };
 * 
 * // Base class for backend IR
 * IRNode
 *   BaseExprNode
 *     ExprNode
 *       Literal
 *         TypedComponentPtr value;
 *       Var
 *         string name;
 *         bool is_ptr;
 *         bool is_tensor;
 *         bool is_parameter; 
 *       Neg
 *         Expr a;
 *       Sqrt
 *         Expr a;
 *       Add
 *         Expr a;
 *         Expr b;
 *       Sub
 *         Expr a;
 *         Expr b;
 *       Mul
 *         Expr a;
 *         Expr b;
 *       Div
 *         Expr a;
 *         Expr b;
 *       Rem
 *         Expr a;
 *         Expr b;
 *       Min
 *         vector<Expr> operands;
 *       Max
 *         vector<Expr> operands;
 *       BitAnd
 *         Expr a;
 *         Expr b;
 *       BitOr
 *         Expr a;
 *         Expr b;
 *       Eq
 *         Expr a;
 *         Expr b;
 *       Neq
 *         Expr a;
 *         Expr b;
 *       Gt
 *         Expr a;
 *         Expr b;
 *       Lt
 *         Expr a;
 *         Expr b;
 *       Gte
 *         Expr a;
 *         Expr b;
 *       Lte
 *         Expr a;
 *         Expr b;
 *       And
 *         Expr a;
 *         Expr b;
 *       Or
 *         Expr a;
 *         Expr b;
 *       BinOp  // [Sparse Array Programming] Generic Binary Op for Ufuncs
 *         Expr a;
 *         Expr b;
 *         string strStart = "";
 *         string strMid = "";
 *         string strEnd = "";
 *       Cast  // Type cast.
 *         Expr a;
 *       Call
 *         string func;
 *         vector<Expr> args;
 *       Load  // A load from an array: arr[loc].
 *         Expr arr;
 *         Expr loc;
 *       Malloc  // Allocate size bytes of memory
 *         Expr size;
 *       Sizeof  // Compute the size of a type
 *         Type sizeofType;
 *       GetProperty  // A tensor property, This unpacks one of the properties of a tensor into an Expr.
 *         Expr tensor;
 *         TensorProperty property;
 *         int mode;
 *         int index = 0;
 *         string name;
 *   BaseStmtNode
 *     StmtNode
 *       Block
 *         vector<Stmt> contents;
 *       Scope
 *         Stmt scopedStmt;
 *       Store  // A store to an array location: arr[loc] = data
 *         Expr arr;
 *         Expr loc;
 *         Expr data;
 *         bool use_atomics;
 *         ParallelUnit atomic_parallel_unit;
 *       IfThenElse
 *         Expr cond;
 *         Stmt then;
 *         Stmt otherwise;
 *       Case  // A series of conditionals.
 *         vector<std::pair<Expr,Stmt>> clauses;
 *         bool alwaysMatch;
 *       Switch  // A switch statement.
 *         vector<pair<Expr,Stmt>> cases;
 *         Expr controlExpr;
 *       For
 *         Expr var;
 *         Expr start;
 *         Expr end;
 *         Expr increment;
 *         Stmt contents;
 *         LoopKind kind;
 *           {Serial, Static, Dynamic, Runtime, Vectorized, Static_Chunked}
 *         int vec_width;  // vectorization width
 *         ParallelUnit parallel_unit;
 *         size_t unrollFactor;
 *       While
 *         Expr cond;
 *         Stmt contents;
 *         LoopKind kind;
 *           {Serial, Static, Dynamic, Runtime, Vectorized, Static_Chunked}
 *         int vec_width;  // vectorization width
 *       Function  // Top-level function for codegen
 *         string name;
 *         Stmt body;
 *         vector<Expr> inputs;
 *         vector<Expr> outputs;
 *       VarDecl
 *         Expr var;
 *         Expr rhs;
 *       Assign
 *         Expr lhs;
 *         Expr rhs;
 *         bool use_atomics;
 *         ParallelUnit atomic_parallel_unit;
 *       Yield  // Yield a result component
 *         vector<Expr> coords;
 *         Expr val;
 *       Allocate  // Allocate memory for a ptr var
 *         Expr var;
 *         Expr num_elements;
 *         Expr old_elements; // used for realloc in CUDA
 *         bool is_realloc;
 *         bool clear; // Whether to use calloc to allocate this memory.
 *       Free
 *         Expr var;
 *       Comment
 *         string text;
 *       BlankLine  // nop
 *       Continue  // Loop continue
 *       Break  // Loop break
 *       Sort
 *         vector<Expr> args;
 *       Print
 *         string fmt;
 *         vector<Expr> params;
 * 
 * 
*/

class CodeGen_Hydride : public CodeGen {
public:
  /// Initialize a code generator that generates code to an
  /// output stream.
  CodeGen_Hydride(std::ostream &dest);
  ~CodeGen_Hydride();

  /// Compile a lowered function
  void compile(Stmt stmt, bool isFirst=false);

  /// Generate shims that unpack an array of pointers representing
  /// a mix of taco_tensor_t* and scalars into a function call
  static void generateShim(const Stmt& func, std::stringstream &stream);

protected:
  using IRPrinter::visit;

  // ExprNode subclasses
  void visit(const Literal*);
  void visit(const Var*);
  void visit(const Neg*);
  void visit(const Sqrt*);
  void visit(const Add*);
  void visit(const Sub*);
  void visit(const Mul*);
  void visit(const Div*);
  void visit(const Rem*);
  void visit(const Min*);
  void visit(const Max*);
  void visit(const BitAnd*);
  void visit(const BitOr*);
  void visit(const Eq*);
  void visit(const Neq*);
  void visit(const Gt*);
  void visit(const Lt*);
  void visit(const Gte*);
  void visit(const Lte*);
  void visit(const And*);
  void visit(const Or*);
  void visit(const BinOp*);
  void visit(const Cast*);
  void visit(const Call*);
  void visit(const Load*);
  void visit(const Malloc*);
  void visit(const Sizeof*);
  void visit(const GetProperty*);

  // StmtNode subclasses
  void visit(const Block*);
  void visit(const Scope*);
  void visit(const IfThenElse*);
  void visit(const Case*);
  void visit(const Switch*);
  void visit(const Store*);
  void visit(const For*);
  void visit(const While*);
  void visit(const Function*);
  void visit(const VarDecl*);
  void visit(const Assign*);
  void visit(const Yield*);
  void visit(const Allocate*);
  void visit(const Free*);
  void visit(const Comment*);
  void visit(const BlankLine*);
  void visit(const Continue*);
  void visit(const Print*);
  void visit(const Sort*);
  void visit(const Break*);

  void printBinaryOp(Expr a, Expr b, std::string bv_name);

  std::map<Expr, std::string, ExprCompare> varMap;
  std::vector<Expr> localVars;
  std::ostream &out;
  
  OutputKind outputKind;

  std::string funcName;
  int labelCount;
  bool emittingCoroutine;

  class FindVars;

private:
  virtual std::string restrictKeyword() const { return "restrict"; }
};

} // namespace ir
} // namespace taco
#endif
