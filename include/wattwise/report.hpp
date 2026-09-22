// report.hpp — energy report rendering: text tables, JSON, Graphviz DOT (M4).
#pragma once
#include <string>
#include <vector>
#include "wattwise/analysis.hpp"
#include "wattwise/optimize/passes.hpp"

namespace ww {

std::string textReport(const EnergyReport& r, const std::string& file, int topN);
std::string comparisonReport(const EnergyReport& before, const EnergyReport& after,
                             const std::vector<PassStats>& passes, const HotSet& hot);
std::string jsonReport(const EnergyReport& r, const EnergyReport* after,
                       const std::vector<PassStats>* passes, const std::string& file);
// One DOT graph per function; blocks shaded by their share of total energy.
std::string dotCFG(const IRFunc& f, const CFG& g, const EnergyReport& r);

}  // namespace ww
