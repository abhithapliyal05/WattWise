// cse.cpp — local common-subexpression elimination (value numbering per
// basic block over pure expressions).
//
// Within a block, `t2 = a op b` is removed when an earlier `t1 = a op b`
// is still available (neither a nor b redefined since). All uses of t2 in the
// function are then renamed to t1. This is safe because both are
// single-assignment temporaries and t1's definition dominates t2's.
// Memory loads are not CSE'd here: that is redundant-load elimination (R3).
#include <map>
#include <tuple>
#include "wattwise/cfg.hpp"
#include "wattwise/optimize/passes.hpp"

namespace ww {

PassStats localCSE(IRFunc& f, const HotSet& hot) {
    PassStats st; st.pass = "local-cse";
    std::map<std::string, int> defs;   // temp -> number of definitions
    for (auto& q : f.code) if (const Operand* d = defOf(q); d && d->k == OK::Temp) defs[d->name]++;
    auto single = [&](const Operand& o) { return o.k == OK::Temp && defs[o.name] == 1; };

    CFG g = buildCFG(f);
    std::map<std::string, Operand> rename;   // eliminated temp -> surviving temp

    for (auto& b : g.blocks) {
        using Key = std::tuple<Op, Operand, Operand>;
        std::map<Key, Operand> avail;
        for (int i = b.begin; i < b.end; ++i) {
            Quad& q = f.code[i];
            // apply renames first so keys see canonical operands
            for (Operand* u : useSlots(q)) { auto it = rename.find(u->name); if (u->k == OK::Temp && it != rename.end()) *u = it->second; }

            // Div/Mod are included: a repeat of a division that did not trap cannot trap
            bool candidate = (isPure(q.op) || q.op == Op::Div || q.op == Op::Mod) && q.op != Op::Copy && single(q.dst);
            if (candidate) {
                Operand a = q.a, bb = q.b;
                if (isCommutative(q.op) && bb < a) std::swap(a, bb);
                Key k{q.op, a, bb};
                auto it = avail.find(k);
                if (it != avail.end() && hot.isHotLine(f.name, q.line)) {
                    st.changes++;
                    st.log.push_back("line " + std::to_string(q.line) + ": " + q.str() + "  =>  reuse " + it->second.str());
                    rename[q.dst.name] = it->second;
                    q.op = Op::Nop;
                    continue;
                }
                avail[k] = q.dst;
            }
            // kill expressions whose operands are redefined here; a call may
            // write any global
            const Operand* d = defOf(q);
            bool call = q.op == Op::Call;
            if (d || call) {
                for (auto it = avail.begin(); it != avail.end();) {
                    const auto& [op, x, y] = it->first;
                    bool dies = (d && (x == *d || y == *d)) || (call && (x.global || y.global));
                    it = dies ? avail.erase(it) : std::next(it);
                }
            }
        }
    }
    // rename remaining uses in the whole function (uses in later blocks)
    for (auto& q : f.code)
        for (Operand* u : useSlots(q))
            if (u->k == OK::Temp) { auto it = rename.find(u->name); while (it != rename.end()) { *u = it->second; it = rename.find(u->name); } }
    compact(f);
    return st;
}

}  // namespace ww
