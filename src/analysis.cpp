// analysis.cpp — the energy analysis pass.
//   block energy  = Σ_{q ∈ B} cost(q) × freq(B)
//   loop energy   = Σ_{B ∈ L} block energy       (inclusive of inner loops)
//   func energy   = Σ_{B ∈ F} block energy       (exclusive of callees)
// Static mode uses estimateStatic(); dynamic mode uses interpreter counts,
// which makes the dynamic figures exact with respect to the cost model.
#include "wattwise/analysis.hpp"
#include <algorithm>

namespace ww {

CFGMap buildAllCFGs(const IRProgram& p) {
    CFGMap m;
    for (auto& f : p.funcs) m[f.name] = buildCFG(f);
    return m;
}

namespace {

std::string blockName(const Block& b) { return b.label.empty() ? "B" + std::to_string(b.id) : b.label; }

// freqOf(func, block) and quadCount(func, quad) abstract over static/dynamic.
template <class BlockFreqFn, class QuadCountFn, class TripFn>
EnergyReport build(const std::string& mode, const IRProgram& p, const CFGMap& cfgs, const CostModel& m,
                   BlockFreqFn blockFreq, QuadCountFn quadCount, TripFn trip, std::map<std::string, double> invocations) {
    EnergyReport r;
    r.mode = mode;
    for (auto& f : p.funcs) {
        const CFG& g = cfgs.at(f.name);
        FuncEnergy fe{f.name, invocations[f.name], 0};
        std::vector<double> bE(g.blocks.size(), 0.0);
        for (auto& b : g.blocks) {
            BlockEnergy be;
            be.func = f.name; be.block = b.id; be.label = blockName(b);
            be.loopDepth = g.depth[b.id];
            be.freq = blockFreq(f, g, b.id);
            be.firstLine = 1 << 30; be.lastLine = 0;
            for (int i = b.begin; i < b.end; ++i) {
                const Quad& q = f.code[i];
                double c = m.cost(q);
                be.costPerExec += c;
                double e = c * quadCount(f, g, i);
                be.energy += e;
                if (e > 0) {
                    r.byCategory[CostModel::category(q)] += e;
                    r.byLine[{f.name, q.line}] += e;
                }
                if (q.op != Op::Label) { be.firstLine = std::min(be.firstLine, q.line); be.lastLine = std::max(be.lastLine, q.line); }
            }
            if (be.lastLine == 0) be.firstLine = 0;
            bE[b.id] = be.energy;
            fe.energy += be.energy;
            r.blocks.push_back(be);
        }
        for (size_t li = 0; li < g.loops.size(); ++li) {
            const Loop& L = g.loops[li];
            LoopEnergy le;
            le.func = f.name;
            le.headerLabel = g.blocks[L.header].label;
            le.depth = L.depth;
            le.headerLine = f.code[g.blocks[L.header].begin].line;
            for (int i = g.blocks[L.header].begin; i < g.blocks[L.header].end; ++i)
                if (f.code[i].op != Op::Label) { le.headerLine = f.code[i].line; break; }
            auto [t, how] = trip(f, g, li);
            le.trip = t; le.tripHow = how;
            for (int b : L.blocks) le.energy += bE[b];
            r.loops.push_back(le);
        }
        r.total += fe.energy;
        r.funcs.push_back(fe);
    }
    auto byE = [](auto& a, auto& b) { return a.energy > b.energy; };
    std::stable_sort(r.blocks.begin(), r.blocks.end(), byE);
    std::stable_sort(r.loops.begin(), r.loops.end(), byE);
    std::stable_sort(r.funcs.begin(), r.funcs.end(), byE);
    return r;
}

}  // namespace

EnergyReport analyzeStatic(const IRProgram& p, const CFGMap& cfgs, const CostModel& m) {
    StaticFreq sf = estimateStatic(p, cfgs);
    std::map<std::string, double> inv;
    for (auto& [n, ff] : sf) inv[n] = ff.invocations;
    auto bf = [&](const IRFunc& f, const CFG&, int b) { return sf.at(f.name).block[b] * sf.at(f.name).invocations; };
    auto qc = [&](const IRFunc& f, const CFG& g, int i) { return sf.at(f.name).block[g.blockOf[i]] * sf.at(f.name).invocations; };
    auto tr = [&](const IRFunc& f, const CFG&, size_t li) {
        const TripInfo& t = sf.at(f.name).loops[li];
        return std::pair<double, std::string>{t.trip, t.how};
    };
    return build("static", p, cfgs, m, bf, qc, tr, inv);
}

EnergyReport analyzeDynamic(const IRProgram& p, const CFGMap& cfgs, const CostModel& m, const RunResult& run) {
    std::map<std::string, double> inv;
    for (auto& f : p.funcs) inv[f.name] = f.code.empty() ? 0 : (double)run.counts.at(f.name)[0];
    auto bf = [&](const IRFunc& f, const CFG& g, int b) { return (double)run.counts.at(f.name)[g.blocks[b].begin]; };
    auto qc = [&](const IRFunc& f, const CFG&, int i) { return (double)run.counts.at(f.name)[i]; };
    auto tr = [&](const IRFunc& f, const CFG& g, size_t li) {
        // measured average trip count = back-edge traversals / loop entries
        const Loop& L = g.loops[li];
        double headerRuns = (double)run.counts.at(f.name)[g.blocks[L.header].begin];
        double latchRuns = 0;
        for (int l : L.latches) latchRuns += (double)run.counts.at(f.name)[g.blocks[l].end - 1];
        double entries = headerRuns - latchRuns;
        return std::pair<double, std::string>{entries > 0 ? latchRuns / entries : 0.0, "measured"};
    };
    return build("dynamic", p, cfgs, m, bf, qc, tr, inv);
}

RankAgreement compareRankings(const EnergyReport& a, const EnergyReport& b, int k) {
    RankAgreement ra; ra.k = k;
    auto top = [&](const EnergyReport& r) {
        std::vector<std::pair<std::string, int>> v;
        for (auto& x : r.blocks) { if ((int)v.size() == k || x.energy <= 0) break; v.push_back({x.func, x.block}); }
        return v;
    };
    auto ta = top(a), tb = top(b);
    for (auto& x : ta) if (std::find(tb.begin(), tb.end(), x) != tb.end()) ra.overlap++;
    ra.topMatches = !ta.empty() && !tb.empty() && ta[0] == tb[0];
    ra.setMatches = ra.overlap == (int)std::min(ta.size(), tb.size()) && ta.size() == tb.size();
    return ra;
}

bool HotSet::isHotLine(const std::string& f, int line) const {
    if (all) return true;
    auto it = lines.find(f);
    return it != lines.end() && std::find(it->second.begin(), it->second.end(), line) != it->second.end();
}
bool HotSet::isHotLoop(const std::string& f, const std::string& h) const {
    if (all) return true;
    auto it = loops.find(f);
    return it != loops.end() && std::find(it->second.begin(), it->second.end(), h) != it->second.end();
}

HotSet selectHot(const EnergyReport& r, double coverage) {
    HotSet hs;
    std::vector<std::pair<double, std::pair<std::string, int>>> lines;
    for (auto& [k, e] : r.byLine) lines.push_back({e, k});
    std::sort(lines.begin(), lines.end(), [](auto& a, auto& b) { return a.first > b.first; });
    double acc = 0;
    for (auto& [e, k] : lines) {
        if (r.total > 0 && acc >= coverage * r.total) break;
        hs.lines[k.first].push_back(k.second);
        acc += e;
    }
    // a loop is hot if it holds ≥ (1 − coverage) of total energy
    for (auto& L : r.loops)
        if (r.total > 0 && L.energy >= (1.0 - coverage) * r.total) hs.loops[L.func].push_back(L.headerLabel);
    return hs;
}

}  // namespace ww
