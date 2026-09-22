// semantic.hpp — scoped symbol table + static type checking (Module M1).
#pragma once
#include <map>
#include <string>
#include <vector>
#include "wattwise/ast.hpp"

namespace ww {

struct FuncSig {
    Ty ret;
    std::vector<Ty> params;
};

struct SemaResult {
    std::map<std::string, FuncSig> funcs;
    std::vector<Diag> warnings;
};

// Resolves every name, assigns unique IR names (shadowed locals become
// "x.1", "x.2", ...), folds `const` scalars, and type-checks the program.
// Throws CompileError("semantic", ...) with all errors found.
SemaResult analyze(Program& p);

}  // namespace ww
