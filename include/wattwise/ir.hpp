// ir.hpp — three-address intermediate representation (Module M2).
//
// A function is a flat vector of quadruples (op, dst, a, b, c). Labels are
// pseudo-instructions. Temporaries ("t12") are created by the generator and
// are single-assignment except for the result temps of && / || (see irgen).
// Textual format: docs/ir_format.md.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "wattwise/common.hpp"

namespace ww {

enum class OK { None, Var, Temp, ConstI, ConstD, Label, Func, Str };

struct Operand {
    OK k = OK::None;
    std::string name;       // Var/Temp/Label/Func name, Str contents
    long long ival = 0;     // ConstI value, or arg count for Call
    double dval = 0.0;      // ConstD value
    Ty ty = Ty::Int;        // Int or Double (bool is represented as Int 0/1)
    bool global = false;    // Var refers to a global

    bool isConst() const { return k == OK::ConstI || k == OK::ConstD; }
    bool isName() const { return k == OK::Var || k == OK::Temp; }
    bool operator==(const Operand& o) const;
    bool operator<(const Operand& o) const;   // ordering for sets/maps
    std::string str() const;

    static Operand var(const std::string& n, Ty t, bool g) { Operand o; o.k = OK::Var; o.name = n; o.ty = t; o.global = g; return o; }
    static Operand temp(const std::string& n, Ty t) { Operand o; o.k = OK::Temp; o.name = n; o.ty = t; return o; }
    static Operand ci(long long v) { Operand o; o.k = OK::ConstI; o.ival = v; o.ty = Ty::Int; return o; }
    static Operand cd(double v) { Operand o; o.k = OK::ConstD; o.dval = v; o.ty = Ty::Double; return o; }
    static Operand label(const std::string& n) { Operand o; o.k = OK::Label; o.name = n; return o; }
};

enum class Op {
    Add, Sub, Mul, Div, Mod, Shl,        // dst = a op b
    Neg, Not,                             // dst = op a
    Lt, Le, Gt, Ge, Eq, Ne,               // dst = a cmp b  (0/1)
    Copy, I2D, D2I,                       // dst = a  (with conversion)
    Load,                                 // dst = a[b]        (a = array Var)
    Store,                                // a[b] = c
    Label,                                // dst: label
    Jmp,                                  // goto dst
    JmpF,                                 // if a == 0 goto dst
    Param,                                // push argument a
    Call,                                 // dst = call a (b.ival = #args); dst may be None
    Ret,                                  // return a (a may be None)
    Print, PrintS, PrintNL,               // cout << a / "str" / endl
    Nop
};

const char* opName(Op o);

struct Quad {
    Op op = Op::Nop;
    Operand dst, a, b, c;
    int line = 0;          // source line, for per-line energy attribution
    std::string str() const;
};

struct ArrayInfo {
    Ty ty = Ty::Int;
    long long len = 0;
    std::vector<double> init;   // global arrays only: initial values
};

struct IRFunc {
    std::string name;
    Ty ret = Ty::Void;
    std::vector<Operand> params;               // Var operands, in order
    std::vector<Quad> code;
    std::map<std::string, ArrayInfo> arrays;   // local arrays
    int nextTemp = 0, nextLabel = 0;           // counters (passes create new ones)
    Operand newTemp(Ty t) { return Operand::temp("t" + std::to_string(++nextTemp), t); }
    Operand newLabel() { return Operand::label("L" + std::to_string(++nextLabel)); }
};

struct IRProgram {
    std::vector<IRFunc> funcs;
    std::map<std::string, ArrayInfo> globalArrays;
    std::map<std::string, Operand> globalScalars;   // name -> initial constant
    IRFunc* find(const std::string& n);
    const IRFunc* find(const std::string& n) const;
    std::string str() const;
};

// ---- def/use helpers used by CFG, data-flow and optimization passes ----
bool isPure(Op o);              // no side effects, cannot trap (Div/Mod excluded)
bool isBranch(Op o);            // Jmp/JmpF/Ret: ends a basic block
bool isCommutative(Op o);
const Operand* defOf(const Quad& q);                 // scalar Var/Temp defined, or null
std::vector<const Operand*> usesOf(const Quad& q);   // scalar Var/Temp operands read
std::vector<Operand*> useSlots(Quad& q);             // mutable version of usesOf

}  // namespace ww
