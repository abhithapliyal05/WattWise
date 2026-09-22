// Shared helpers for the WattWise unit tests.
#pragma once
#include <string>
#include "wattwise/analysis.hpp"
#include "wattwise/interpreter.hpp"
#include "wattwise/irgen.hpp"
#include "wattwise/lexer.hpp"
#include "wattwise/optimize/passes.hpp"
#include "wattwise/parser.hpp"
#include "wattwise/semantic.hpp"

namespace wwt {
inline ww::IRProgram compile(const std::string& src) {
    ww::Program p = ww::parse(ww::lex(src));
    ww::SemaResult s = ww::analyze(p);
    return ww::generateIR(p, s);
}
// Returns the phase name of the first CompileError, or "" if it compiles.
inline std::string failPhase(const std::string& src) {
    try { compile(src); } catch (const ww::CompileError& e) { return e.phase(); }
    return "";
}
inline std::string run(const ww::IRProgram& p) { return ww::interpret(p).output; }
inline int countOp(const ww::IRFunc& f, ww::Op o) {
    int n = 0;
    for (auto& q : f.code) if (q.op == o) ++n;
    return n;
}
inline ww::HotSet allHot() { ww::HotSet h; h.all = true; return h; }
}  // namespace wwt
