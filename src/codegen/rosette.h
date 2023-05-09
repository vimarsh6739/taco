#ifndef TACO_BACKEND_ROSETTE_H
#define TACO_BACKEND_ROSETTE_H
#include <map>
#include <vector>

#include "taco/ir/ir.h"
#include "taco/ir/ir_printer.h"
#include "codegen.h"

namespace taco {
namespace ir {

Stmt optimize_instructions_synthesis(Stmt stmt, bool& mutated_expr);

/**
 * 
 * High-level Overview of TACO IR
 * 
 * enum IRNodeType {
 *   Literal, Var, Neg, Sqrt, Add, Sub, Mul, Div, Rem, Min, Max, BitAnd, BitOr, 
 *   Not, Eq, Neq, Gt, Lt, Gte, Lte, And, Or, BinOp, Cast, Call, IfThenElse,
 *   Case, Switch, Load, Malloc, Sizeof, Store, For, While, Block, Scope, 
 *   Function, VarDecl, VarAssign, Yield, Allocate, Free, Comment, BlankLine, 
 *   Print, GetProperty, Continue, Sort, Break
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
 *           { Order, Dimension, ComponentSize, ModeOrdering, 
 *             ModeTypes, Indices, Values, FillValue, ValuesSize };
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
 *         vector<pair<Expr,Stmt>> clauses;
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
 *           { Serial, Static, Dynamic, Runtime, Vectorized, Static_Chunked }
 *         int vec_width;  // vectorization width
 *         ParallelUnit parallel_unit;
 *         size_t unrollFactor;
 *       While
 *         Expr cond;
 *         Stmt contents;
 *         LoopKind kind;
 *           { Serial, Static, Dynamic, Runtime, Vectorized, Static_Chunked }
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
  /// Compile a lowered function
  // void compile(Stmt stmt, bool isFirst=false);


} // namespace ir
} // namespace taco
#endif
