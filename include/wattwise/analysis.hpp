// analysis.hpp — energy analysis pass: block / loop / function / line energy
// and hotspot ranking (Module M3).
#pragma once
#include <map>
#include <string>
#include <vector>
#include "wattwise/cfg.hpp"
#include "wattwise/energy_model.hpp"
#include "wattwise/frequency.hpp"
#include "wattwise/interpreter.hpp"

namespace ww {

struct BlockEnergy {
    std::string func;
    int block = 0;
    std::string label;          // first label in the block, or "B<n>"
    int firstLine = 0, lastLine = 0;
    int loopDepth = 0;
    double freq = 0;            // total executions (all invocations)
    double costPerExec = 0;     // Σ op costs in the block
    double energy = 0;          // freq × costPerExec (pJ)
};

struct LoopEnergy {
    std::string func;
    std::string headerLabel;
    int headerLine = 0;
    int depth = 1;
    double trip = 0;            // static estimate, or measured average (dynamic)
    std::string tripHow;
    double energy = 0;          // inclusive: all blocks in the loop
};

struct FuncEnergy {
    std::string name;
    double invocations = 0;
    double energy = 0;          // exclusive: the function's own blocks
};

struct EnergyReport {
    std::string mode;                           // "static" or "dynamic"
    double total = 0;
    std::vector<BlockEnergy> blocks;            // sorted by energy, descending
    std::vector<LoopEnergy> loops;              // sorted by energy, descending
    std::vector<FuncEnergy> funcs;              // sorted by energy, descending
    std::map<std::string, double> byCategory;   // cost-table key -> pJ
    std::map<std::pair<std::string, int>, double> byLine;   // (func, line) -> pJ
};

using CFGMap = std::map<std::string, CFG>;
CFGMap buildAllCFGs(const IRProgram& p);

EnergyReport analyzeStatic(const IRProgram& p, const CFGMap& cfgs, const CostModel& m);
EnergyReport analyzeDynamic(const IRProgram& p, const CFGMap& cfgs, const CostModel& m, const RunResult& run);

// Hotspot agreement between two rankings of the same program (objective O3):
// does the set of the top-k blocks match, and does #1 match?
struct RankAgreement { int k = 3; int overlap = 0; bool topMatches = false; bool setMatches = false; };
RankAgreement compareRankings(const EnergyReport& a, const EnergyReport& b, int k = 3);

// Profile-directed optimisation: the smallest set of source lines (and the
// loops containing them) that covers `coverage` of total energy.
struct HotSet {
    std::map<std::string, std::vector<int>> lines;          // func -> hot lines
    std::map<std::string, std::vector<std::string>> loops;  // func -> hot loop header labels
    bool isHotLine(const std::string& f, int line) const;
    bool isHotLoop(const std::string& f, const std::string& header) const;
    bool all = false;                                       // --opt-all: everything is hot
};
HotSet selectHot(const EnergyReport& r, double coverage = 0.90);

}  // namespace ww
