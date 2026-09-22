// common.hpp — shared types, diagnostics and error reporting for WattWise.
#pragma once
#include <stdexcept>
#include <string>
#include <vector>

namespace ww {

// Scalar types of the WattWise C++ subset. Arrays are a property of a symbol,
// not a separate type.
enum class Ty { Int, Double, Bool, Void, Error };

const char* tyName(Ty t);

// A single compiler diagnostic, located in the source.
struct Diag {
    int line = 0, col = 0;
    std::string msg;
    bool warning = false;
    std::string str() const;
};

// Thrown by any front-end phase when one or more errors were found.
// Phases collect several diagnostics before throwing, so the user sees
// every error of a phase at once.
class CompileError : public std::runtime_error {
public:
    CompileError(std::string phase, std::vector<Diag> d);
    const std::vector<Diag>& diags() const { return diags_; }
    const std::string& phase() const { return phase_; }
private:
    std::string phase_;   // "lexical", "syntax", "semantic"
    std::vector<Diag> diags_;
};

}  // namespace ww
