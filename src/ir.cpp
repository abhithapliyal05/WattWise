// ir.cpp — IR printing and def/use helpers.
#include "wattwise/ir.hpp"
#include <sstream>
#include <tuple>

namespace ww {

bool Operand::operator==(const Operand& o) const {
    if (k != o.k) return false;
    switch (k) {
        case OK::ConstI: return ival == o.ival;
        case OK::ConstD: return dval == o.dval;
        case OK::Var: return name == o.name && global == o.global;
        case OK::None: return true;
        default: return name == o.name;
    }
}
bool Operand::operator<(const Operand& o) const {
    return std::tie(k, name, global, ival, dval) < std::tie(o.k, o.name, o.global, o.ival, o.dval);
}

static std::string fmtDouble(double d) {
    std::ostringstream s;
    s.precision(17);
    s << d;
    std::string r = s.str();
    if (r.find_first_of(".eEn") == std::string::npos) r += ".0";
    return r;
}

std::string Operand::str() const {
    switch (k) {
        case OK::None: return "_";
        case OK::Var: return global ? "@" + name : name;
        case OK::Temp: case OK::Label: case OK::Func: return name;
        case OK::ConstI: return std::to_string(ival);
        case OK::ConstD: return fmtDouble(dval);
        case OK::Str: {
            std::string r = "\"";
            for (char ch : name) {
                if (ch == '\n') r += "\\n"; else if (ch == '\t') r += "\\t";
                else if (ch == '"') r += "\\\""; else if (ch == '\\') r += "\\\\"; else r += ch;
            }
            return r + "\"";
        }
    }
    return "?";
}

const char* opName(Op o) {
    switch (o) {
        case Op::Add: return "add"; case Op::Sub: return "sub"; case Op::Mul: return "mul";
        case Op::Div: return "div"; case Op::Mod: return "mod"; case Op::Shl: return "shl";
        case Op::Neg: return "neg"; case Op::Not: return "not";
        case Op::Lt: return "lt"; case Op::Le: return "le"; case Op::Gt: return "gt";
        case Op::Ge: return "ge"; case Op::Eq: return "eq"; case Op::Ne: return "ne";
        case Op::Copy: return "copy"; case Op::I2D: return "i2d"; case Op::D2I: return "d2i";
        case Op::Load: return "load"; case Op::Store: return "store"; case Op::Label: return "label";
        case Op::Jmp: return "jmp"; case Op::JmpF: return "jmpf"; case Op::Param: return "param";
        case Op::Call: return "call"; case Op::Ret: return "ret"; case Op::Print: return "print";
        case Op::PrintS: return "prints"; case Op::PrintNL: return "printnl"; case Op::Nop: return "nop";
    }
    return "?";
}

static const char* sym(Op o) {
    switch (o) {
        case Op::Add: return "+"; case Op::Sub: return "-"; case Op::Mul: return "*";
        case Op::Div: return "/"; case Op::Mod: return "%"; case Op::Shl: return "<<";
        case Op::Lt: return "<"; case Op::Le: return "<="; case Op::Gt: return ">";
        case Op::Ge: return ">="; case Op::Eq: return "=="; case Op::Ne: return "!=";
        default: return "?";
    }
}

std::string Quad::str() const {
    switch (op) {
        case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: case Op::Mod: case Op::Shl:
        case Op::Lt: case Op::Le: case Op::Gt: case Op::Ge: case Op::Eq: case Op::Ne:
            return dst.str() + " = " + a.str() + " " + sym(op) + " " + b.str();
        case Op::Neg: return dst.str() + " = -" + a.str();
        case Op::Not: return dst.str() + " = !" + a.str();
        case Op::Copy: return dst.str() + " = " + a.str();
        case Op::I2D: return dst.str() + " = (double) " + a.str();
        case Op::D2I: return dst.str() + " = (int) " + a.str();
        case Op::Load: return dst.str() + " = " + a.str() + "[" + b.str() + "]";
        case Op::Store: return a.str() + "[" + b.str() + "] = " + c.str();
        case Op::Label: return dst.name + ":";
        case Op::Jmp: return "goto " + dst.name;
        case Op::JmpF: return "iffalse " + a.str() + " goto " + dst.name;
        case Op::Param: return "param " + a.str();
        case Op::Call: return (dst.k == OK::None ? "" : dst.str() + " = ") + "call " + a.name + ", " + std::to_string(b.ival);
        case Op::Ret: return a.k == OK::None ? "return" : "return " + a.str();
        case Op::Print: return "print " + a.str();
        case Op::PrintS: return "print " + a.str();
        case Op::PrintNL: return "print endl";
        case Op::Nop: return "nop";
    }
    return "?";
}

IRFunc* IRProgram::find(const std::string& n) {
    for (auto& f : funcs) if (f.name == n) return &f;
    return nullptr;
}
const IRFunc* IRProgram::find(const std::string& n) const {
    for (auto& f : funcs) if (f.name == n) return &f;
    return nullptr;
}

std::string IRProgram::str() const {
    std::string o;
    for (auto& [n, v] : globalScalars) o += "global " + std::string(tyName(v.ty)) + " @" + n + " = " + v.str() + "\n";
    for (auto& [n, a] : globalArrays) o += "global " + std::string(tyName(a.ty)) + " @" + n + "[" + std::to_string(a.len) + "]\n";
    if (!globalScalars.empty() || !globalArrays.empty()) o += "\n";
    for (auto& f : funcs) {
        o += "func " + f.name + "(";
        for (size_t i = 0; i < f.params.size(); ++i) { if (i) o += ", "; o += f.params[i].str(); }
        o += ") -> " + std::string(tyName(f.ret)) + "\n";
        for (auto& [n, a] : f.arrays) o += "  array " + std::string(tyName(a.ty)) + " " + n + "[" + std::to_string(a.len) + "]\n";
        for (auto& q : f.code) o += (q.op == Op::Label ? "" : "    ") + q.str() + "\n";
        o += "end\n\n";
    }
    return o;
}

bool isPure(Op o) {
    switch (o) {
        case Op::Add: case Op::Sub: case Op::Mul: case Op::Shl: case Op::Neg: case Op::Not:
        case Op::Lt: case Op::Le: case Op::Gt: case Op::Ge: case Op::Eq: case Op::Ne:
        case Op::Copy: case Op::I2D: case Op::D2I:
            return true;
        default:
            return false;
    }
}
bool isBranch(Op o) { return o == Op::Jmp || o == Op::JmpF || o == Op::Ret; }
bool isCommutative(Op o) { return o == Op::Add || o == Op::Mul || o == Op::Eq || o == Op::Ne; }

const Operand* defOf(const Quad& q) {
    switch (q.op) {
        case Op::Label: case Op::Jmp: case Op::JmpF: case Op::Param: case Op::Store:
        case Op::Ret: case Op::Print: case Op::PrintS: case Op::PrintNL: case Op::Nop:
            return nullptr;
        default:
            return q.dst.isName() ? &q.dst : nullptr;
    }
}

std::vector<Operand*> useSlots(Quad& q) {
    std::vector<Operand*> u;
    auto add = [&](Operand& o) { if (o.isName()) u.push_back(&o); };
    switch (q.op) {
        case Op::Label: case Op::Jmp: case Op::PrintS: case Op::PrintNL: case Op::Nop: case Op::Call:
            break;
        case Op::Load: add(q.b); break;               // a is the array name (not a scalar use)
        case Op::Store: add(q.b); add(q.c); break;
        default: add(q.a); add(q.b); break;
    }
    return u;
}
std::vector<const Operand*> usesOf(const Quad& q) {
    auto s = useSlots(const_cast<Quad&>(q));
    return {s.begin(), s.end()};
}

}  // namespace ww
