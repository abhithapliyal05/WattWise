// cfg.hpp — basic blocks, control-flow graph, dominators, natural loops and
// liveness (Module M2; liveness is used by the M3 optimizer).
#pragma once
#include <set>
#include <string>
#include <vector>
#include "wattwise/ir.hpp"

namespace ww {

struct Block {
    int id = 0;
    int begin = 0, end = 0;          // quad index range [begin, end)
    std::vector<int> succ, pred;
    std::string label;               // leading label, if any
    bool reachable = false;
};

struct Loop {
    int header = 0;
    std::set<int> blocks;            // includes the header
    std::vector<int> latches;        // sources of back-edges
    std::vector<int> exiting;        // blocks in the loop with a successor outside
    std::vector<int> exitTargets;    // successors outside the loop
    int depth = 1;                   // 1 = outermost
    int parent = -1;                 // index of the enclosing loop, or -1
};

struct CFG {
    std::vector<Block> blocks;
    std::vector<int> blockOf;                  // quad index -> block id
    std::vector<std::vector<char>> dom;        // dom[b][d] != 0  <=>  d dominates b
    std::vector<Loop> loops;                   // sorted: inner loops first
    std::vector<int> depth;                    // loop nesting depth of each block
    std::vector<int> innermost;                // innermost loop index of each block, or -1

    bool dominates(int d, int b) const { return dom[b][d] != 0; }
};

// Leaders: first quad, every label, every quad after a jump/return.
CFG buildCFG(const IRFunc& f);

struct Liveness {
    std::vector<std::set<Operand>> in, out;    // local scalars and temps only
};

// Classic backward data-flow: in[B] = use[B] ∪ (out[B] − def[B]),
// out[B] = ∪ in[S] over successors. Globals are excluded (never optimised).
Liveness computeLiveness(const IRFunc& f, const CFG& g);

}  // namespace ww
