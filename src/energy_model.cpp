#include "wattwise/energy_model.hpp"
#include <fstream>
#include <sstream>

namespace ww {

CostModel CostModel::defaults() {
    CostModel m;
    m.t_ = {
        {"mem.load", 10.0},  {"mem.store", 12.0},    // L1-resident data access incl. address generation
        {"int.alu", 1.0},    {"int.shift", 1.0},     // add/sub/neg/not/compare
        {"int.mul", 3.0},    {"int.div", 18.0},      // div/mod: multi-cycle, unpipelined
        {"fp.alu", 2.5},     {"fp.mul", 3.5},        {"fp.div", 14.0},
        {"convert", 2.0},    {"move", 0.8},          // copy / int<->double
        {"branch.uncond", 0.6}, {"branch.cond", 0.9},
        {"call", 6.0},       {"ret", 4.0},           {"param", 0.8},
        {"io", 0.0},         {"none", 0.0}           // I/O is out of scope (R1)
    };
    return m;
}

bool CostModel::loadFile(const std::string& path, std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "cannot open cost file '" + path + "'"; return false; }
    std::string line;
    int ln = 0;
    while (std::getline(in, line)) {
        ++ln;
        if (auto h = line.find('#'); h != std::string::npos) line.resize(h);
        std::istringstream ss(line);
        std::string key; double v;
        if (!(ss >> key)) continue;
        if (!(ss >> v) || v < 0) { err = path + ":" + std::to_string(ln) + ": expected '<key> <non-negative number>'"; return false; }
        if (!t_.count(key)) { err = path + ":" + std::to_string(ln) + ": unknown cost key '" + key + "'"; return false; }
        t_[key] = v;
    }
    return true;
}

std::string CostModel::category(const Quad& q) {
    bool fp = q.dst.ty == Ty::Double || q.a.ty == Ty::Double;
    switch (q.op) {
        case Op::Add: case Op::Sub: case Op::Neg: case Op::Lt: case Op::Le: case Op::Gt:
        case Op::Ge: case Op::Eq: case Op::Ne:
            return fp ? "fp.alu" : "int.alu";
        case Op::Not: return "int.alu";
        case Op::Shl: return "int.shift";
        case Op::Mul: return fp ? "fp.mul" : "int.mul";
        case Op::Div: return fp ? "fp.div" : "int.div";
        case Op::Mod: return "int.div";
        case Op::Copy: return "move";
        case Op::I2D: case Op::D2I: return "convert";
        case Op::Load: return "mem.load";
        case Op::Store: return "mem.store";
        case Op::Jmp: return "branch.uncond";
        case Op::JmpF: return "branch.cond";
        case Op::Call: return "call";
        case Op::Ret: return "ret";
        case Op::Param: return "param";
        case Op::Print: case Op::PrintS: case Op::PrintNL: return "io";
        default: return "none";
    }
}

double CostModel::cost(const Quad& q) const { return t_.at(category(q)); }

}  // namespace ww
