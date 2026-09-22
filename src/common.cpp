#include "wattwise/common.hpp"

namespace ww {

const char* tyName(Ty t) {
    switch (t) {
        case Ty::Int: return "int";
        case Ty::Double: return "double";
        case Ty::Bool: return "bool";
        case Ty::Void: return "void";
        default: return "<error>";
    }
}

std::string Diag::str() const {
    return std::to_string(line) + ":" + std::to_string(col) + ": " +
           (warning ? "warning: " : "error: ") + msg;
}

static std::string joinDiags(const std::string& phase, const std::vector<Diag>& d) {
    std::string s = phase + " error(s):\n";
    for (auto& x : d) s += "  " + x.str() + "\n";
    return s;
}

CompileError::CompileError(std::string phase, std::vector<Diag> d)
    : std::runtime_error(joinDiags(phase, d)), phase_(std::move(phase)), diags_(std::move(d)) {}

}  // namespace ww
