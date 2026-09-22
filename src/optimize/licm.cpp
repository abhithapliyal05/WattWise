// licm.cpp — loop-invariant code motion, guarded by dominators and liveness.
//
// For each hot natural loop L (innermost first), an instruction  d = a op b
// in L is hoisted into a new preheader when:
//   (1) op is pure and cannot trap (int div/mod only by a constant ≠ 0, −1);
//   (2) every operand is a constant, or has no definition inside L, or is
//       defined by an instruction already chosen for hoisting; globals count
//       as invariant only if L contains no store to them and no call;
//   (3a) d is a temporary with exactly one definition in the function, or
//   (3b) d is a local variable defined exactly once in L, d is not live on
//        entry to the header (no use sees a pre-loop value), and either the
//        defining block dominates every exiting block or d is dead at every
//        loop exit (so zero-trip executions stay correct).
// The preheader is a fresh label placed just before the header; every jump
// from outside L to the header is redirected to it. The pass iterates to a
// fixed point, so code hoisted into an inner preheader can move out further.
#include <algorithm>
#include <map>
#include <set>
#include "wattwise/cfg.hpp"
#include "wattwise/optimize/passes.hpp"

namespace ww {
namespace {

bool hoistableOp(const Quad& q) {
    if (q.op == Op::D2I) return false;   // may trap on out-of-range values
    if (isPure(q.op)) return true;
    if ((q.op == Op::Div || q.op == Op::Mod) && q.dst.ty == Ty::Int)
        return q.b.k == OK::ConstI && q.b.ival != 0 && q.b.ival != -1;
    if (q.op == Op::Div && q.dst.ty == Ty::Double) return true;   // IEEE: no trap
    return false;
}

// One hoisting round; returns true if the code changed.
bool hoistOnce(IRFunc& f, const HotSet& hot, PassStats& st) {
    CFG g = buildCFG(f);
    Liveness lv = computeLiveness(f, g);

    std::map<std::string, int> tempDefs;
    for (auto& q : f.code) if (const Operand* d = defOf(q); d && d->k == OK::Temp) tempDefs[d->name]++;

    for (auto& L : g.loops) {
        const Block& H = g.blocks[L.header];
        if (H.label.empty() || !hot.isHotLoop(f.name, H.label)) continue;
        // the block laid out just before the header must not be part of the
        // loop and fall through into it (the preheader would sit in between)
        if (H.begin > 0) {
            int prev = g.blockOf[H.begin - 1];
            Op lastOp = f.code[H.begin - 1].op;
            if (L.blocks.count(prev) && lastOp != Op::Jmp && lastOp != Op::Ret) continue;
        }

        // definitions inside the loop
        std::map<Operand, int> defsInLoop;
        bool hasCall = false;
        std::vector<int> quads;
        for (int b : L.blocks)
            for (int i = g.blocks[b].begin; i < g.blocks[b].end; ++i) {
                quads.push_back(i);
                if (const Operand* d = defOf(f.code[i])) defsInLoop[*d]++;
                if (f.code[i].op == Op::Call) hasCall = true;
            }
        std::sort(quads.begin(), quads.end());

        std::set<int> chosen;
        std::set<Operand> hoistedDefs;
        auto invariant = [&](const Operand& o) {
            if (o.k == OK::None || o.isConst()) return true;
            if (!o.isName()) return false;
            if (o.global && hasCall) return false;
            if (hoistedDefs.count(o)) return true;
            return defsInLoop.find(o) == defsInLoop.end();
        };
        bool grew = true;
        while (grew) {
            grew = false;
            for (int i : quads) {
                if (chosen.count(i)) continue;
                const Quad& q = f.code[i];
                if (!hoistableOp(q) || !hot.isHotLine(f.name, q.line)) continue;
                const Operand* d = defOf(q);
                if (!d || d->global) continue;
                if (!invariant(q.a) || !invariant(q.b)) continue;
                if (d->k == OK::Temp) {
                    if (tempDefs[d->name] != 1) continue;
                } else {
                    if (defsInLoop[*d] != 1) continue;
                    if (lv.in[L.header].count(*d)) continue;
                    int db = g.blockOf[i];
                    bool domAll = std::all_of(L.exiting.begin(), L.exiting.end(), [&](int e) { return g.dominates(db, e); });
                    bool deadAtExits = std::none_of(L.exitTargets.begin(), L.exitTargets.end(), [&](int t) { return lv.in[t].count(*d) > 0; });
                    if (!domAll && !deadAtExits) continue;
                }
                chosen.insert(i);
                hoistedDefs.insert(*d);
                grew = true;
            }
        }
        if (chosen.empty()) continue;

        // ---- transform ----
        std::set<std::string> headerLabels;
        for (int i = H.begin; i < H.end && f.code[i].op == Op::Label; ++i) headerLabels.insert(f.code[i].dst.name);
        Operand pre = f.newLabel();
        std::set<int> inLoop(quads.begin(), quads.end());
        for (size_t i = 0; i < f.code.size(); ++i) {
            Quad& q = f.code[i];
            if ((q.op == Op::Jmp || q.op == Op::JmpF) && headerLabels.count(q.dst.name) && !inLoop.count((int)i))
                q.dst = pre;   // entries from outside the loop now go through the preheader
        }
        std::vector<Quad> out;
        out.reserve(f.code.size() + 1);
        for (int i = 0; i < (int)f.code.size(); ++i) {
            if (i == H.begin) {
                Quad lab; lab.op = Op::Label; lab.dst = pre; lab.line = f.code[H.begin].line;
                out.push_back(lab);
                for (int c : chosen) {
                    out.push_back(f.code[c]);
                    st.changes++;
                    st.log.push_back("line " + std::to_string(f.code[c].line) + ": hoisted '" + f.code[c].str() +
                                     "' out of loop " + H.label + " into " + pre.name);
                }
            }
            if (!chosen.count(i)) out.push_back(f.code[i]);
        }
        f.code = std::move(out);
        return true;   // CFG is stale: rebuild and continue
    }
    return false;
}

}  // namespace

PassStats licm(IRFunc& f, const HotSet& hot) {
    PassStats st; st.pass = "licm";
    int guard = 0;
    while (hoistOnce(f, hot, st) && ++guard < 1000) {}
    return st;
}

std::vector<PassStats> optimize(IRProgram& p, const HotSet& hot, const std::vector<std::string>& passes) {
    std::vector<std::string> seq;
    for (auto& s : passes) {
        if (s == "all") { seq.insert(seq.end(), {"sr", "cse", "licm", "cse"}); }
        else seq.push_back(s);
    }
    std::vector<PassStats> stats;
    for (auto& name : seq) {
        PassStats total; total.pass = name;
        for (auto& f : p.funcs) {
            PassStats s;
            if (name == "sr") s = strengthReduce(f, hot);
            else if (name == "cse") s = localCSE(f, hot);
            else if (name == "licm") s = licm(f, hot);
            else throw std::runtime_error("unknown pass '" + name + "' (available: sr, cse, licm, all)");
            total.pass = s.pass;
            total.changes += s.changes;
            for (auto& l : s.log) total.log.push_back(f.name + ": " + l);
        }
        stats.push_back(std::move(total));
    }
    return stats;
}

}  // namespace ww
