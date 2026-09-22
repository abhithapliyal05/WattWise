// ast.hpp — abstract syntax tree for the WattWise C++ subset.
// The parser builds it; the semantic analyzer annotates it in place
// (types, resolved unique names, constant values).
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "wattwise/common.hpp"

namespace ww {

enum class EK { IntLit, FloatLit, BoolLit, Var, Index, Unary, Binary, Call };

struct Expr {
    EK kind;
    int line = 0, col = 0;
    std::string op;            // operator text for Unary/Binary
    std::string name;          // identifier for Var/Index/Call
    long long ival = 0;
    double dval = 0.0;
    std::vector<std::unique_ptr<Expr>> kids;  // operands / index / call args

    // ---- filled by semantic analysis ----
    Ty ty = Ty::Error;
    std::string uname;         // unique IR name of the referenced symbol
    bool isGlobal = false;
    bool isConst = false;      // reference to a `const` scalar: folded to constant
    long long constI = 0;
    double constD = 0.0;
};
using ExprP = std::unique_ptr<Expr>;

enum class SK { VarDecl, Assign, IncDec, ExprStmt, If, While, For, Return, Block, Print, Empty };

// One item of a `cout << a << "x" << endl;` chain.
struct PrintItem {
    enum Kind { Value, Str, Endl } kind;
    ExprP expr;       // Value
    std::string str;  // Str
};

struct Stmt {
    SK kind;
    int line = 0, col = 0;

    // VarDecl
    Ty declTy = Ty::Error;
    bool isConst = false;
    std::string name;
    ExprP arraySize;                    // non-null for arrays
    ExprP init;                         // scalar initializer
    std::vector<ExprP> initList;        // array initializer {..}
    bool hasInitList = false;
    long long arrayLen = 0;             // filled by semantic
    std::string uname;                  // filled by semantic
    bool isGlobal = false;              // filled by semantic

    // Assign / IncDec: target is a Var or Index expression
    ExprP target;
    std::string op;                     // "=", "+=", ..., "++", "--"
    ExprP value;

    // ExprStmt / If / While / Return
    ExprP expr;

    // If / While / For / Block
    std::vector<std::unique_ptr<Stmt>> body;       // Block statements
    std::unique_ptr<Stmt> thenS, elseS, loopBody;
    std::unique_ptr<Stmt> forInit, forStep;

    // Print
    std::vector<PrintItem> items;
};
using StmtP = std::unique_ptr<Stmt>;

struct Param { Ty ty; std::string name; int line = 0, col = 0; std::string uname; };

struct FuncDecl {
    Ty ret;
    std::string name;
    std::vector<Param> params;
    StmtP body;
    int line = 0, col = 0;
};

struct Program {
    std::vector<StmtP> globals;                    // VarDecl statements
    std::vector<std::unique_ptr<FuncDecl>> funcs;
    // Source order of top-level items: {isFunc, index into globals/funcs}.
    // C++ requires declaration before use, so semantic analysis walks this.
    std::vector<std::pair<bool, size_t>> order;
};

// Debug dump used by `wattwise --ast`.
std::string dumpAst(const Program& p);

}  // namespace ww
