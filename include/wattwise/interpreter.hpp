// interpreter.hpp — IR interpreter = dynamic profiler (Module M4).
//
// Executes the three-address code directly and counts how many times every
// quad runs. Those counts give (a) exact dynamic frequencies for the energy
// model and (b) the program's observable output, used for differential
// testing (original C++ via g++ vs. unoptimised IR vs. optimised IR).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "wattwise/ir.hpp"

namespace ww {

struct RunLimits {
    uint64_t maxSteps = 2'000'000'000ULL;
    int maxDepth = 10000;
};

struct RunResult {
    bool ok = true;
    std::string error;                 // runtime error message (div by zero, bounds, ...)
    std::string output;                // everything printed via cout
    int exitCode = 0;                  // main's return value
    uint64_t steps = 0;                // quads executed
    std::map<std::string, std::vector<uint64_t>> counts;   // func -> per-quad execution count
};

RunResult interpret(const IRProgram& p, const RunLimits& lim = {});

}  // namespace ww
