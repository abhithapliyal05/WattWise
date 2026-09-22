// energy_model.hpp — instruction-level energy cost model (Module M2).
//
// E(program) = Σ_q cost(op(q)) × executions(q)          [Tiwari et al. 1994]
// Costs are in model picojoules. The table is RELATIVE: it encodes the
// ordering memory ≫ divide > multiply > ALU > move > branch, which is what
// ranking hotspots and comparing before/after needs. See docs/cost_model.md.
#pragma once
#include <map>
#include <string>
#include "wattwise/ir.hpp"

namespace ww {

class CostModel {
public:
    static CostModel defaults();
    // Overrides entries from a "key value" text file ('#' comments).
    // Returns false and sets err on a malformed line or unknown key.
    bool loadFile(const std::string& path, std::string& err);

    double cost(const Quad& q) const;          // model pJ per execution
    static std::string category(const Quad& q);  // cost-table key used for q
    const std::map<std::string, double>& table() const { return t_; }

private:
    std::map<std::string, double> t_;
};

}  // namespace ww
