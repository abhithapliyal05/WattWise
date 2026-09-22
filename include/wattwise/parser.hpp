// parser.hpp — recursive-descent parser (Module M1).
// Grammar: docs/subset.md (EBNF). One function per non-terminal.
#pragma once
#include <vector>
#include "wattwise/ast.hpp"
#include "wattwise/lexer.hpp"

namespace ww {

// Throws CompileError("syntax", ...). Uses panic-mode recovery: on an error
// the parser records it, skips to the next ';' or '}' and keeps going, so
// several syntax errors are reported in one run.
Program parse(const std::vector<Token>& toks);

}  // namespace ww
