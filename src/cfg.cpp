// cfg.cpp — CFG construction, iterative dominators, natural-loop detection.
#include "wattwise/cfg.hpp"
#include <algorithm>
#include <map>

namespace ww {

CFG buildCFG(const IRFunc& f) {
    CFG g;
    const auto& code = f.code;
    const int n = static_cast<int>(code.size());
    g.blockOf.assign(n, -1);

    // 1. leaders
    std::vector<char> leader(n + 1, 0);
    if (n > 0) leader[0] = 1;
    for (int i = 0; i < n; ++i) {
        if (code[i].op == Op::Label) leader[i] = 1;
        if (isBranch(code[i].op) && i + 1 < n) leader[i + 1] = 1;
    }
    // 2. blocks
    std::map<std::string, int> labelBlock;
    for (int i = 0; i < n;) {
        Block b;
        b.id = static_cast<int>(g.blocks.size());
        b.begin = i;
        int j = i + 1;
        while (j < n && !leader[j]) ++j;
        b.end = j;
        if (code[i].op == Op::Label) b.label = code[i].dst.name;
        for (int k = i; k < j; ++k) g.blockOf[k] = b.id;
        // consecutive labels belong to the same block's name set
        for (int k = i; k < j && code[k].op == Op::Label; ++k) labelBlock[code[k].dst.name] = b.id;
        g.blocks.push_back(b);
        i = j;
    }
    // Labels in the middle of a block cannot exist (every label is a leader),
    // but a block may begin with several labels: handled above.
    // 3. edges
    const int nb = static_cast<int>(g.blocks.size());
    for (auto& b : g.blocks) {
        const Quad& last = code[b.end - 1];
        auto addEdge = [&](int to) {
            if (std::find(b.succ.begin(), b.succ.end(), to) == b.succ.end()) b.succ.push_back(to);
        };
        if (last.op == Op::Jmp) addEdge(labelBlock.at(last.dst.name));
        else if (last.op == Op::JmpF) {
            if (b.id + 1 < nb) addEdge(b.id + 1);
            addEdge(labelBlock.at(last.dst.name));
        } else if (last.op != Op::Ret) {
            if (b.id + 1 < nb) addEdge(b.id + 1);
        }
    }
    for (auto& b : g.blocks)
        for (int s : b.succ) g.blocks[s].pred.push_back(b.id);

    // 4. reachability
    if (nb > 0) {
        std::vector<int> st{0};
        g.blocks[0].reachable = true;
        while (!st.empty()) {
            int x = st.back(); st.pop_back();
            for (int s : g.blocks[x].succ)
                if (!g.blocks[s].reachable) { g.blocks[s].reachable = true; st.push_back(s); }
        }
    }

    // 5. dominators (iterative data-flow; entry dominated only by itself)
    g.dom.assign(nb, std::vector<char>(nb, 1));
    if (nb > 0) { std::fill(g.dom[0].begin(), g.dom[0].end(), 0); g.dom[0][0] = 1; }
    bool changed = true;
    while (changed) {
        changed = false;
        for (int b = 1; b < nb; ++b) {
            if (!g.blocks[b].reachable) continue;
            std::vector<char> nd(nb, 1);
            bool any = false;
            for (int p : g.blocks[b].pred) {
                if (!g.blocks[p].reachable) continue;
                any = true;
                for (int d = 0; d < nb; ++d) nd[d] = nd[d] && g.dom[p][d];
            }
            if (!any) std::fill(nd.begin(), nd.end(), 0);
            nd[b] = 1;
            if (nd != g.dom[b]) { g.dom[b] = std::move(nd); changed = true; }
        }
    }

    // 6. natural loops from back-edges (n -> h where h dominates n)
    std::map<int, Loop> byHeader;
    for (auto& b : g.blocks) {
        if (!b.reachable) continue;
        for (int h : b.succ) {
            if (!g.dominates(h, b.id)) continue;
            Loop& L = byHeader[h];
            L.header = h;
            L.latches.push_back(b.id);
            L.blocks.insert(h);
            std::vector<int> st;
            if (L.blocks.insert(b.id).second) st.push_back(b.id);
            while (!st.empty()) {
                int x = st.back(); st.pop_back();
                for (int p : g.blocks[x].pred)
                    if (g.blocks[p].reachable && L.blocks.insert(p).second) st.push_back(p);
            }
        }
    }
    for (auto& [h, L] : byHeader) g.loops.push_back(std::move(L));
    // inner loops first (fewer blocks), so optimisation can go inside-out
    std::sort(g.loops.begin(), g.loops.end(), [](const Loop& a, const Loop& b) {
        return a.blocks.size() != b.blocks.size() ? a.blocks.size() < b.blocks.size() : a.header < b.header;
    });
    for (size_t i = 0; i < g.loops.size(); ++i) {
        Loop& L = g.loops[i];
        for (int x : L.blocks)
            for (int s : g.blocks[x].succ)
                if (!L.blocks.count(s)) {
                    if (std::find(L.exiting.begin(), L.exiting.end(), x) == L.exiting.end()) L.exiting.push_back(x);
                    if (std::find(L.exitTargets.begin(), L.exitTargets.end(), s) == L.exitTargets.end()) L.exitTargets.push_back(s);
                }
        for (size_t j = i + 1; j < g.loops.size(); ++j) {   // smallest strict superset = parent
            const Loop& P = g.loops[j];
            if (P.blocks.size() > L.blocks.size() && std::includes(P.blocks.begin(), P.blocks.end(), L.blocks.begin(), L.blocks.end())) {
                L.parent = static_cast<int>(j);
                break;
            }
        }
    }
    g.depth.assign(nb, 0);
    g.innermost.assign(nb, -1);
    for (size_t i = 0; i < g.loops.size(); ++i)
        for (int x : g.loops[i].blocks) {
            g.depth[x]++;
            if (g.innermost[x] == -1) g.innermost[x] = static_cast<int>(i);  // loops sorted inner-first
        }
    for (auto& L : g.loops) L.depth = g.depth[L.header];
    return g;
}

Liveness computeLiveness(const IRFunc& f, const CFG& g) {
    const int nb = static_cast<int>(g.blocks.size());
    std::vector<std::set<Operand>> use(nb), def(nb);
    auto local = [](const Operand& o) { return o.isName() && !o.global; };
    for (auto& b : g.blocks) {
        for (int i = b.begin; i < b.end; ++i) {
            for (const Operand* u : usesOf(f.code[i]))
                if (local(*u) && !def[b.id].count(*u)) use[b.id].insert(*u);
            if (const Operand* d = defOf(f.code[i]); d && local(*d)) def[b.id].insert(*d);
        }
    }
    Liveness lv;
    lv.in.assign(nb, {});
    lv.out.assign(nb, {});
    bool changed = true;
    while (changed) {
        changed = false;
        for (int b = nb - 1; b >= 0; --b) {
            std::set<Operand> out;
            for (int s : g.blocks[b].succ) out.insert(lv.in[s].begin(), lv.in[s].end());
            std::set<Operand> in = use[b];
            for (auto& x : out) if (!def[b].count(x)) in.insert(x);
            if (in != lv.in[b] || out != lv.out[b]) { lv.in[b] = std::move(in); lv.out[b] = std::move(out); changed = true; }
        }
    }
    return lv;
}

}  // namespace ww
