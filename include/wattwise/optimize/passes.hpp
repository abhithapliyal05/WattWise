// passes.hpp — energy-aware optimisation passes (Module M3).
//
// Review 2 status: strength reduction (SR), local common-subexpression
// elimination (CSE) and loop-invariant code motion (LICM) are implemented.
// Dead-code elimination (DCE) and redundant-load elimination (RLE) are the
// remaining two passes planned for Review 3.
//
// Every pass is profile-directed: it only rewrites instructions on hot source
// lines / loops selected by selectHot() (or everything with HotSet::all).
#pragma once
#include <string>
#include <vector>
#include "wattwise/analysis.hpp"
#include "wattwise/ir.hpp"

namespace ww {

struct PassStats {
    std::string pass;
    int changes = 0;
    std::vector<std::string> log;   // one line per transformation
};

PassStats strengthReduce(IRFunc& f, const HotSet& hot);
PassStats localCSE(IRFunc& f, const HotSet& hot);
PassStats licm(IRFunc& f, const HotSet& hot);

// Runs the named passes in order over every function; "all" = sr,cse,licm,cse.
std::vector<PassStats> optimize(IRProgram& p, const HotSet& hot, const std::vector<std::string>& passes);

// Removes Nop quads left behind by passes.
void compact(IRFunc& f);

}  // namespace ww
