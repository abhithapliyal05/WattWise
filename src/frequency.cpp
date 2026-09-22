// frequency.cpp — static trip-count and block-frequency estimation.
#include "wattwise/frequency.hpp"
#include <cmath>
#include <functional>
#include <algorithm>
#include <optional>
#include <set>

namespace ww {
namespace {

struct KnownConsts {
    // local vars assigned exactly once in the function with an int constant
    std::map<std::string, long long> locals;
    // globals never assigned anywhere in the program
    std::map<std::string, long long> globals;
};

std::optional<long long> constOf(const Operand& o, const KnownConsts& k) {
    if (o.k == OK::ConstI) return o.ival;
    if (o.k == OK::Var && !o.global) { auto it = k.locals.find(o.name); if (it != k.locals.end()) return it->second; }
    if (o.k == OK::Var && o.global) { auto it = k.globals.find(o.name); if (it != k.globals.end()) return it->second; }
    return std::nullopt;
}

TripInfo tripCount(const IRFunc& f, const CFG& g, const Loop& L, const KnownConsts& kc) {
    TripInfo ti;
    ti.how = "default (no analysable induction variable)";
    const Block& h = g.blocks[L.header];
    const Quad& br = f.code[h.end - 1];
    if (br.op != Op::JmpF) return ti;
    // the compare that feeds the exit branch, inside the header block
    const Quad* cmp = nullptr;
    for (int i = h.end - 2; i >= h.begin; --i) {
        const Operand* d = defOf(f.code[i]);
        if (d && *d == br.a) { cmp = &f.code[i]; break; }
    }
    if (!cmp || cmp->op < Op::Lt || cmp->op > Op::Ne || cmp->op == Op::Eq) return ti;

    // find a local var in the compare that is updated once in the loop by ±const
    auto stepOf = [&](const Operand& v, long long& step) -> bool {
        if (v.k != OK::Var || v.global) return false;
        int defs = 0; bool ok = false;
        for (int b : L.blocks)
            for (int i = g.blocks[b].begin; i < g.blocks[b].end; ++i) {
                const Quad& q = f.code[i];
                const Operand* d = defOf(q);
                if (!d || !(*d == v)) continue;
                ++defs;
                if ((q.op == Op::Add || q.op == Op::Sub) && q.a == v && q.b.k == OK::ConstI) {
                    step = q.op == Op::Add ? q.b.ival : -q.b.ival; ok = true;
                }
            }
        return defs == 1 && ok && step != 0;
    };
    Op op = cmp->op;
    Operand iv = cmp->a, bound = cmp->b;
    long long step = 0;
    if (!stepOf(iv, step)) {
        if (!stepOf(cmp->b, step)) return ti;
        std::swap(iv, bound);                    // bound OP iv  ==>  iv OP' bound
        op = op == Op::Lt ? Op::Gt : op == Op::Le ? Op::Ge : op == Op::Gt ? Op::Lt : op == Op::Ge ? Op::Le : op;
    }
    auto B = constOf(bound, kc);
    if (!B) { ti.how = "default (bound '" + bound.str() + "' not a known constant)"; return ti; }

    // initial value: nearest preceding definition of iv in layout order
    std::optional<long long> I;
    for (int i = h.begin - 1; i >= 0; --i) {
        const Operand* d = defOf(f.code[i]);
        if (d && *d == iv) { if (f.code[i].op == Op::Copy) I = constOf(f.code[i].a, kc); break; }
    }
    auto ceilDiv = [](long long a, long long b) { return a <= 0 ? 0LL : (a + b - 1) / b; };
    long long b = *B;
    if (!I) {
        ti.trip = std::max(1.0, std::fabs((double)b) / 2.0 / std::fabs((double)step));
        ti.how = "approx: bound " + std::to_string(b) + ", start unknown (triangular) -> bound/2";
        return ti;
    }
    long long i0 = *I, n = -1;
    switch (op) {
        case Op::Lt: if (step > 0) n = ceilDiv(b - i0, step); break;
        case Op::Le: if (step > 0) n = ceilDiv(b - i0 + 1, step); break;
        case Op::Gt: if (step < 0) n = ceilDiv(i0 - b, -step); break;
        case Op::Ge: if (step < 0) n = ceilDiv(i0 - b + 1, -step); break;
        case Op::Ne: if ((b - i0) % step == 0 && (b - i0) / step >= 0) n = (b - i0) / step; break;
        default: break;
    }
    if (n < 0) return ti;
    ti.trip = (double)n;
    ti.exact = true;
    ti.how = iv.str() + " from " + std::to_string(i0) + " step " + std::to_string(step) + " vs " + std::to_string(b);
    return ti;
}

}  // namespace

StaticFreq estimateStatic(const IRProgram& p, const std::map<std::string, CFG>& cfgs) {
    StaticFreq sf;

    // program-wide: globals that are never written keep their initial value
    std::set<std::string> writtenGlobals;
    for (auto& f : p.funcs)
        for (auto& q : f.code)
            if (const Operand* d = defOf(q); d && d->global) writtenGlobals.insert(d->name);
    KnownConsts base;
    for (auto& [n, v] : p.globalScalars)
        if (!writtenGlobals.count(n) && v.k == OK::ConstI) base.globals[n] = v.ival;

    for (auto& f : p.funcs) {
        const CFG& g = cfgs.at(f.name);
        KnownConsts kc = base;
        std::map<std::string, int> defCount;
        std::map<std::string, std::optional<long long>> defVal;
        for (auto& q : f.code)
            if (const Operand* d = defOf(q); d && d->k == OK::Var && !d->global) {
                defCount[d->name]++;
                defVal[d->name] = (q.op == Op::Copy && q.a.k == OK::ConstI) ? std::optional<long long>(q.a.ival) : std::nullopt;
            }
        for (auto& pr : f.params) defCount[pr.name]++;   // params are defined by the caller
        for (auto& [n, c] : defCount) if (c == 1 && defVal[n]) kc.locals[n] = *defVal[n];

        FuncFreq ff;
        for (auto& L : g.loops) ff.loops.push_back(tripCount(f, g, L, kc));

        std::vector<int> rets;
        for (auto& b : g.blocks) if (b.reachable && f.code[b.end - 1].op == Op::Ret) rets.push_back(b.id);
        auto probIn = [&](int b, int loop) {
            const std::vector<int>& targets = loop < 0 ? rets : g.loops[loop].latches;
            for (int t : targets) if (!g.dominates(b, t)) return 0.5;
            return 1.0;
        };
        ff.block.assign(g.blocks.size(), 0.0);
        for (auto& b : g.blocks) {
            if (!b.reachable) continue;
            double fr = 1.0;
            for (size_t li = 0; li < g.loops.size(); ++li)
                if (g.loops[li].blocks.count(b.id))
                    fr *= ff.loops[li].trip + (g.loops[li].header == b.id ? 1.0 : 0.0);
            int L = g.innermost[b.id];
            double pr = probIn(b.id, L);
            while (L >= 0) { pr *= probIn(g.loops[L].header, g.loops[L].parent); L = g.loops[L].parent; }
            ff.block[b.id] = fr * pr;
        }
        sf[f.name] = std::move(ff);
    }

    // ---- invocation counts over the call graph ----
    std::map<std::string, std::map<std::string, double>> calls;   // caller -> callee -> calls/invocation
    for (auto& f : p.funcs) {
        const CFG& g = cfgs.at(f.name);
        for (size_t i = 0; i < f.code.size(); ++i)
            if (f.code[i].op == Op::Call)
                calls[f.name][f.code[i].a.name] += sf[f.name].block[g.blockOf[i]];
    }
    std::map<std::string, std::set<std::string>> reach;
    for (auto& f : p.funcs) {
        std::vector<std::string> st{f.name};
        while (!st.empty()) {
            auto x = st.back(); st.pop_back();
            for (auto& [c, _] : calls[x]) if (reach[f.name].insert(c).second) st.push_back(c);
        }
    }
    auto sameSCC = [&](const std::string& a, const std::string& b) { return reach[a].count(b) && reach[b].count(a); };
    // topological order (callers first) over the DAG of SCCs, via DFS post-order
    std::vector<std::string> order;
    std::set<std::string> seen;
    std::function<void(const std::string&)> dfs = [&](const std::string& x) {
        if (!seen.insert(x).second) return;
        for (auto& [c, _] : calls[x]) if (!sameSCC(x, c)) dfs(c);
        order.push_back(x);
    };
    for (auto& f : p.funcs) dfs(f.name);
    std::reverse(order.begin(), order.end());
    for (auto& n : order) {
        double inv = n == "main" ? 1.0 : 0.0;
        for (auto& [caller, callees] : calls) {
            if (sameSCC(caller, n) && caller != n) continue;
            if (caller == n) continue;
            auto it = callees.find(n);
            if (it != callees.end()) inv += it->second * sf[caller].invocations;
        }
        if (reach[n].count(n)) inv *= kRecursionFactor;   // recursive: unknown depth
        sf[n].invocations = inv;
    }
    return sf;
}

}  // namespace ww
