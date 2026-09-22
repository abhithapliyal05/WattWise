// strength.cpp — strength reduction and algebraic simplification.
//
// Rewrites (int = 32-bit, fp = IEEE double; each is exact):
//   x * 2^k   -> x << k                 (int)
//   x * 2.0   -> x + x                  (fp)
//   x / 2^k   -> x * 2^-k               (fp: power-of-two reciprocal is exact)
//   x * 1, x / 1, x + 0, x - 0 -> x     (x + 0 only for int: -0.0 + 0.0 = +0.0)
//   x * 0     -> 0                      (int only: NaN * 0 is NaN)
//   c1 op c2  -> constant               (int, wrap-around; never folds / 0)
// Integer x / 2^k is NOT turned into a shift: for negative x, C++ truncates
// toward zero while an arithmetic shift rounds toward −∞.
#include <cmath>
#include "wattwise/optimize/passes.hpp"

namespace ww {
namespace {

int log2Exact(long long v) {
    if (v <= 0 || (v & (v - 1))) return -1;
    int k = 0;
    while ((1LL << k) != v) ++k;
    return k;
}
bool isPow2D(double v, int& k) {
    if (v <= 0 || !std::isfinite(v)) return false;
    int e; double m = std::frexp(v, &e);
    if (m != 0.5) return false;
    k = e - 1;
    return k >= -1000 && k <= 1000;
}
long long wrap32(long long v) { return (long long)(int32_t)(uint32_t)v; }

}  // namespace

PassStats strengthReduce(IRFunc& f, const HotSet& hot) {
    PassStats st; st.pass = "strength-reduction";
    for (auto& q : f.code) {
        if (!hot.isHotLine(f.name, q.line)) continue;
        std::string before = q.str();
        bool fp = q.dst.ty == Ty::Double;
        auto toCopy = [&](const Operand& v) { q.op = Op::Copy; q.a = v; q.b = {}; };

        // constant folding (int)
        if (!fp && q.a.k == OK::ConstI && q.b.k == OK::ConstI &&
            (q.op == Op::Add || q.op == Op::Sub || q.op == Op::Mul || q.op == Op::Shl ||
             ((q.op == Op::Div || q.op == Op::Mod) && q.b.ival != 0 && q.b.ival != -1))) {
            long long a = q.a.ival, b = q.b.ival, r = 0;
            switch (q.op) {
                case Op::Add: r = a + b; break; case Op::Sub: r = a - b; break; case Op::Mul: r = a * b; break;
                case Op::Div: r = a / b; break; case Op::Mod: r = a % b; break;
                default: r = (long long)((uint32_t)a << (b & 31)); break;
            }
            toCopy(Operand::ci(wrap32(r)));
        } else if (q.op == Op::Mul) {
            if (isCommutative(q.op) && q.a.isConst() && !q.b.isConst()) std::swap(q.a, q.b);  // const on the right
            if (!fp && q.b.k == OK::ConstI) {
                long long c = q.b.ival;
                int k = log2Exact(c);
                if (c == 0) toCopy(Operand::ci(0));
                else if (c == 1) toCopy(q.a);
                else if (k > 0) { q.op = Op::Shl; q.b = Operand::ci(k); }
            } else if (fp && q.b.k == OK::ConstD) {
                if (q.b.dval == 1.0) toCopy(q.a);
                else if (q.b.dval == 2.0) { q.op = Op::Add; q.b = q.a; }
            }
        } else if (q.op == Op::Div) {
            if (!fp && q.b.k == OK::ConstI && q.b.ival == 1) toCopy(q.a);
            else if (fp && q.b.k == OK::ConstD) {
                int k;
                if (q.b.dval == 1.0) toCopy(q.a);
                else if (isPow2D(q.b.dval, k)) { q.op = Op::Mul; q.b = Operand::cd(std::ldexp(1.0, -k)); }
            }
        } else if ((q.op == Op::Add || q.op == Op::Sub) && !fp) {
            if (q.op == Op::Add && q.a.k == OK::ConstI && q.a.ival == 0) toCopy(q.b);
            else if (q.b.k == OK::ConstI && q.b.ival == 0) toCopy(q.a);
        }
        std::string after = q.str();
        if (after != before) { st.changes++; st.log.push_back("line " + std::to_string(q.line) + ": " + before + "  =>  " + after); }
    }
    return st;
}

void compact(IRFunc& f) {
    std::vector<Quad> out;
    out.reserve(f.code.size());
    for (auto& q : f.code) if (q.op != Op::Nop) out.push_back(std::move(q));
    f.code = std::move(out);
}

}  // namespace ww
