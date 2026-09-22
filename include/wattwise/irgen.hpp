// irgen.hpp — syntax-directed translation AST -> three-address code (Module M2).
#pragma once
#include "wattwise/ast.hpp"
#include "wattwise/ir.hpp"
#include "wattwise/semantic.hpp"

namespace ww {

// Requires a Program that passed analyze(). Never fails on valid input.
IRProgram generateIR(const Program& p, const SemaResult& sema);

}  // namespace ww
