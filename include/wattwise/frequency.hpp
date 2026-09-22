// frequency.hpp — static execution-frequency estimation (Module M3·M4).
//
// Per invocation of a function, block b is estimated to run
//     freq(b) = Π_{loops L ∋ b} (trip(L) + [b is L's header]) × p(b)
// trip(L): derived from the loop's induction variable when the header test is
//          `iv <op> bound` with a constant step and constant/known bounds;
//          otherwise kDefaultTrip.
// p(b)   : 1 if b dominates every latch of its innermost loop (or every
//          return, outside loops), else 0.5 (unknown branch).
// Invocation counts propagate over the call graph from main (recursive
// functions × kRecursionFactor).
#pragma once
#include <map>
#include <string>
#include <vector>
#include "wattwise/cfg.hpp"
#include "wattwise/ir.hpp"

namespace ww {

constexpr double kDefaultTrip = 10.0;
constexpr double kRecursionFactor = 10.0;

struct TripInfo {
    double trip = kDefaultTrip;
    bool exact = false;         // derived from constants
    std::string how;            // human-readable derivation
};

struct FuncFreq {
    std::vector<double> block;          // per invocation
    std::vector<TripInfo> loops;        // parallel to CFG::loops
    double invocations = 0;
};

using StaticFreq = std::map<std::string, FuncFreq>;

StaticFreq estimateStatic(const IRProgram& p, const std::map<std::string, CFG>& cfgs);

}  // namespace ww
