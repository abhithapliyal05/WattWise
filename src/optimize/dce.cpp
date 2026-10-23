// dce.cpp — dead-code elimination.
//
// Three clean-ups, all driven by the CFG and backward liveness:
//   1. unreachable blocks (no path from the entry) are deleted. They execute
//      zero times, so this is energy-neutral and shrinks the IR; it is applied
//      to every function regardless of the hot set.
//   2. dead assignments: an instruction whose only effect is to define a local
//      scalar or temporary that is not live afterwards is deleted, repeating
//      until nothing changes so whole dead chains disappear. Only hot lines
//      are rewritten (profile direction). Removable operations are the pure
//      ones (D2I excluded), loads, FP division, and integer div/mod by a
//      constant other than 0 and -1. A dead load or division could only
//      trap; WattWise optimises a program only after a profiling run of the
//      original completed without a runtime error, and the subset has no
//      input, so the removed instruction never trapped.
//   3. self-moves (x = x) are deleted.
// Stores, calls, I/O, branches and anything defining a global are never removed.
#include <set>
#include "wattwise/cfg.hpp"
#include "wattwise/optimize/passes.hpp"

namespace ww {
namespace {

bool removable(const Quad& q) {
    if (q.op == Op::D2I) return false;
    if (isPure(q.op) || q.op == Op::Load) return true;
    if (q.op == Op::Div && q.dst.ty == Ty::Double) return true;
    if ((q.op == Op::Div || q.op == Op::Mod) && q.dst.ty == Ty::Int)
        return q.b.k == OK::ConstI && q.b.ival != 0 && q.b.ival != -1;
    return false;
}

bool isLocalName(const Operand& o) { return o.isName() && !o.global; }

}  // namespace

PassStats deadCode(IRFunc& f, const HotSet& hot) {
    PassStats st; st.pass = "dead-code-elim";

    {   // 1. unreachable blocks
        CFG g = buildCFG(f);
        bool any = false;
        for (auto& b : g.blocks) {
            if (b.reachable) continue;
            for (int i = b.begin; i < b.end; ++i) {
                if (f.code[i].op == Op::Nop) continue;
                st.changes++;
                st.log.push_back("line " + std::to_string(f.code[i].line) + ": unreachable '" + f.code[i].str() + "' removed");
                f.code[i].op = Op::Nop;
                any = true;
            }
        }
        if (any) compact(f);
    }

    // 2./3. dead assignments and self-moves, to a fixed point
    for (int round = 0; round < 1000; ++round) {
        CFG g = buildCFG(f);
        Liveness lv = computeLiveness(f, g);
        bool changed = false;
        for (auto& b : g.blocks) {
            if (!b.reachable) continue;
            std::set<Operand> live = lv.out[b.id];
            for (int i = b.end - 1; i >= b.begin; --i) {
                Quad& q = f.code[i];
                if (q.op == Op::Nop) continue;
                const Operand* d = defOf(q);
                bool hotLine = hot.isHotLine(f.name, q.line);
                bool selfMove = q.op == Op::Copy && d && q.a == *d;
                bool dead = d && isLocalName(*d) && !live.count(*d) && removable(q);
                if ((dead || selfMove) && hotLine) {
                    st.changes++;
                    st.log.push_back("line " + std::to_string(q.line) + ": removed " + (selfMove ? "self-move" : "dead") + " '" + q.str() + "'");
                    q.op = Op::Nop;
                    changed = true;
                    continue;   // its operands stay dead unless something else uses them
                }
                if (d && isLocalName(*d)) live.erase(*d);
                for (const Operand* u : usesOf(q)) if (isLocalName(*u)) live.insert(*u);
            }
        }
        if (!changed) break;
        compact(f);
    }
    return st;
}

}  // namespace ww
