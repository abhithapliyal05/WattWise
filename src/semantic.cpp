// semantic.cpp — semantic analysis for the WattWise C++ subset.
//
// Symbol table: a stack of scopes (vector<map<name, Symbol>>). Lookup walks
// from the innermost scope outwards; the global scope is at index 0.
// Every rule enforced here keeps accepted programs valid, well-defined C++,
// which is what lets tests compare WattWise against g++ directly.
#include "wattwise/semantic.hpp"
#include <cmath>
#include <optional>

namespace ww {
namespace {

struct Symbol {
    Ty ty = Ty::Error;
    bool isArray = false;
    long long len = 0;
    bool isConst = false;
    bool hasConstVal = false;
    long long ci = 0;
    double cd = 0;
    std::string uname;
    bool global = false;
    int line = 0;
};

struct ConstVal { Ty ty; long long i; double d; };

bool numeric(Ty t) { return t == Ty::Int || t == Ty::Double || t == Ty::Bool; }
bool integral(Ty t) { return t == Ty::Int || t == Ty::Bool; }

class Sema {
public:
    explicit Sema(Program& p) : prog_(p) {}

    SemaResult run() {
        scopes_.emplace_back();  // global scope
        for (auto [isFunc, idx] : prog_.order) {
            if (isFunc) funcDecl(*prog_.funcs[idx]);
            else globalDecl(*prog_.globals[idx]);
        }
        auto it = res_.funcs.find("main");
        if (it == res_.funcs.end()) err(1, 1, "program has no 'int main()' function");
        else if (it->second.ret != Ty::Int || !it->second.params.empty())
            err(mainLine_, 1, "'main' must be declared as 'int main()'");
        if (!errs_.empty()) throw CompileError("semantic", errs_);
        return std::move(res_);
    }

private:
    Program& prog_;
    SemaResult res_;
    std::vector<Diag> errs_;
    std::vector<std::map<std::string, Symbol>> scopes_;
    std::map<std::string, int> nameCount_;  // per function, for unique names
    const FuncDecl* curFn_ = nullptr;
    int mainLine_ = 1;

    void err(int l, int c, const std::string& m) { errs_.push_back({l, c, m, false}); }
    void warn(int l, int c, const std::string& m) { res_.warnings.push_back({l, c, m, true}); }

    Symbol* lookup(const std::string& n) {
        for (int i = static_cast<int>(scopes_.size()) - 1; i >= 0; --i) {
            auto it = scopes_[i].find(n);
            if (it != scopes_[i].end()) return &it->second;
        }
        return nullptr;
    }

    std::string uniqueName(const std::string& n) {
        int k = nameCount_[n]++;
        return k == 0 ? n : n + "." + std::to_string(k);
    }

    // ---------- constant evaluation (array sizes, global/const initializers) ----------
    std::optional<ConstVal> evalConst(const Expr& e) {
        switch (e.kind) {
            case EK::IntLit: return ConstVal{Ty::Int, e.ival, (double)e.ival};
            case EK::BoolLit: return ConstVal{Ty::Int, e.ival, (double)e.ival};
            case EK::FloatLit: return ConstVal{Ty::Double, 0, e.dval};
            case EK::Var: {
                Symbol* s = lookup(e.name);
                if (s && s->hasConstVal && !s->isArray) return ConstVal{s->ty == Ty::Double ? Ty::Double : Ty::Int, s->ci, s->cd};
                return std::nullopt;
            }
            case EK::Unary: {
                auto v = evalConst(*e.kids[0]);
                if (!v) return std::nullopt;
                if (e.op == "-") return ConstVal{v->ty, -v->i, -v->d};
                if (e.op == "!") { bool z = v->ty == Ty::Double ? v->d == 0 : v->i == 0; return ConstVal{Ty::Int, z, (double)z}; }
                return std::nullopt;
            }
            case EK::Binary: {
                auto a = evalConst(*e.kids[0]), b = evalConst(*e.kids[1]);
                if (!a || !b) return std::nullopt;
                if (a->ty == Ty::Int && b->ty == Ty::Int) {
                    long long x = a->i, y = b->i, r;
                    if (e.op == "+") r = x + y; else if (e.op == "-") r = x - y; else if (e.op == "*") r = x * y;
                    else if (e.op == "/" && y != 0) r = x / y; else if (e.op == "%" && y != 0) r = x % y;
                    else return std::nullopt;
                    return ConstVal{Ty::Int, r, (double)r};
                }
                double x = a->ty == Ty::Double ? a->d : a->i, y = b->ty == Ty::Double ? b->d : b->i, r;
                if (e.op == "+") r = x + y; else if (e.op == "-") r = x - y; else if (e.op == "*") r = x * y;
                else if (e.op == "/" && y != 0) r = x / y; else return std::nullopt;
                return ConstVal{Ty::Double, (long long)r, r};
            }
            default: return std::nullopt;
        }
    }

    // ---------- declarations ----------
    void declare(Stmt& d, bool global) {
        auto& scope = scopes_.back();
        if (scope.count(d.name)) { err(d.line, d.col, "redeclaration of '" + d.name + "' in the same scope"); return; }
        if (global && res_.funcs.count(d.name)) { err(d.line, d.col, "'" + d.name + "' is already declared as a function"); return; }

        Symbol s;
        s.ty = d.declTy; s.isConst = d.isConst; s.global = global; s.line = d.line;
        s.uname = global ? d.name : uniqueName(d.name);

        if (d.arraySize) {
            s.isArray = true;
            expr(*d.arraySize);
            auto v = evalConst(*d.arraySize);
            if (!v || v->ty != Ty::Int) err(d.line, d.col, "array size of '" + d.name + "' must be an integer constant expression");
            else if (v->i <= 0) err(d.line, d.col, "array '" + d.name + "' must have a positive size");
            else if (v->i > 10000000) err(d.line, d.col, "array '" + d.name + "' is too large (limit 10,000,000 elements)");
            else s.len = v->i;
            if (d.init) err(d.line, d.col, "array '" + d.name + "' must be initialised with a brace list");
            if (d.hasInitList) {
                if ((long long)d.initList.size() > s.len && s.len > 0)
                    err(d.line, d.col, "too many initializers for array '" + d.name + "'");
                for (auto& x : d.initList) {
                    Ty t = expr(*x);
                    if (!numeric(t)) err(x->line, x->col, "array initializer must be numeric");
                    if (global && !evalConst(*x)) err(x->line, x->col, "global array initializer must be a constant");
                }
            }
        } else {
            if (d.hasInitList) err(d.line, d.col, "brace initializer used on scalar '" + d.name + "'");
            if (d.init) {
                Ty t = expr(*d.init);
                checkAssignable(d.declTy, t, d.init->line, d.init->col, d.name);
                auto v = evalConst(*d.init);
                if (global && !v) err(d.init->line, d.init->col, "global initializer for '" + d.name + "' must be a constant expression");
                if (d.isConst && v) {
                    s.hasConstVal = true;
                    if (d.declTy == Ty::Double) { s.cd = v->ty == Ty::Double ? v->d : (double)v->i; s.ci = (long long)s.cd; }
                    else { s.ci = v->ty == Ty::Double ? (long long)v->d : v->i; if (d.declTy == Ty::Bool) s.ci = s.ci != 0; s.cd = (double)s.ci; }
                }
            }
            if (d.isConst && !d.init) err(d.line, d.col, "const variable '" + d.name + "' must be initialised");
        }
        d.uname = s.uname; d.arrayLen = s.len; d.isGlobal = global;
        scope[d.name] = s;
    }

    void globalDecl(Stmt& d) { declare(d, true); }

    void funcDecl(FuncDecl& f) {
        if (res_.funcs.count(f.name)) { err(f.line, f.col, "redefinition of function '" + f.name + "' (overloading is not in the subset)"); return; }
        if (scopes_[0].count(f.name)) { err(f.line, f.col, "'" + f.name + "' is already declared as a variable"); return; }
        if (f.name == "main") mainLine_ = f.line;
        FuncSig sig{f.ret, {}};
        for (auto& p : f.params) sig.params.push_back(p.ty);
        res_.funcs[f.name] = sig;  // registered before the body: allows recursion

        curFn_ = &f;
        nameCount_.clear();
        scopes_.emplace_back();
        for (auto& p : f.params) {
            if (p.ty == Ty::Void) { err(p.line, p.col, "parameter '" + p.name + "' declared void"); continue; }
            if (scopes_.back().count(p.name)) { err(p.line, p.col, "duplicate parameter '" + p.name + "'"); continue; }
            Symbol s; s.ty = p.ty; s.uname = uniqueName(p.name); s.line = p.line;
            p.uname = s.uname;
            scopes_.back()[p.name] = s;
        }
        // The body block shares the parameter scope, as in C++.
        for (auto& s : f.body->body) stmt(*s);
        scopes_.pop_back();

        if (f.ret != Ty::Void && f.name != "main") {
            if (f.body->body.empty() || !alwaysReturns(*f.body))
                warn(f.line, f.col, "control may reach the end of non-void function '" + f.name + "'");
        }
        curFn_ = nullptr;
    }

    static bool alwaysReturns(const Stmt& s) {
        switch (s.kind) {
            case SK::Return: return true;
            case SK::Block: return !s.body.empty() && alwaysReturns(*s.body.back());
            case SK::If: return s.elseS && alwaysReturns(*s.thenS) && alwaysReturns(*s.elseS);
            default: return false;
        }
    }

    void checkAssignable(Ty dst, Ty src, int l, int c, const std::string& what) {
        if (src == Ty::Error || dst == Ty::Error) return;
        if (src == Ty::Void) { err(l, c, "void value cannot be assigned to '" + what + "'"); return; }
        if (!numeric(src)) { err(l, c, "incompatible value for '" + what + "'"); return; }
        if (dst == Ty::Int && src == Ty::Double) warn(l, c, "implicit conversion from double to int for '" + what + "' may lose precision");
    }

    // ---------- statements ----------
    void stmt(Stmt& s) {
        switch (s.kind) {
            case SK::VarDecl: declare(s, false); break;
            case SK::Block:
                scopes_.emplace_back();
                for (auto& x : s.body) stmt(*x);
                scopes_.pop_back();
                break;
            case SK::Assign: {
                Ty tt = target(*s.target);
                Ty vt = expr(*s.value);
                if (s.op == "%=" && (!integral(tt) || !integral(vt)))
                    err(s.line, s.col, "operands of '%=' must be integers");
                if ((s.op == "/=" || s.op == "%=")) {
                    auto v = evalConst(*s.value);
                    if (v && ((v->ty == Ty::Int && v->i == 0))) err(s.value->line, s.value->col, "division by constant zero");
                }
                checkAssignable(tt, vt, s.value->line, s.value->col, s.target->name);
                break;
            }
            case SK::IncDec: {
                Ty tt = target(*s.target);
                if (tt != Ty::Error && !numeric(tt)) err(s.line, s.col, "'" + s.op + "' needs a numeric operand");
                break;
            }
            case SK::ExprStmt:
                expr(*s.expr, /*allowVoid=*/true);
                break;
            case SK::If:
                cond(*s.expr);
                scoped(*s.thenS);
                if (s.elseS) scoped(*s.elseS);
                break;
            case SK::While:
                cond(*s.expr);
                scoped(*s.loopBody);
                break;
            case SK::For:
                scopes_.emplace_back();  // the for-init variable lives here
                if (s.forInit) stmt(*s.forInit);
                if (s.expr) cond(*s.expr);
                if (s.forStep) stmt(*s.forStep);
                scoped(*s.loopBody);
                scopes_.pop_back();
                break;
            case SK::Return: {
                Ty rt = curFn_->ret;
                if (!s.expr) {
                    if (rt != Ty::Void) err(s.line, s.col, "non-void function '" + curFn_->name + "' must return a value");
                } else {
                    Ty t = expr(*s.expr, true);
                    if (rt == Ty::Void) err(s.line, s.col, "void function '" + curFn_->name + "' cannot return a value");
                    else checkAssignable(rt, t, s.expr->line, s.expr->col, "return value");
                }
                break;
            }
            case SK::Print:
                for (auto& it : s.items)
                    if (it.kind == PrintItem::Value) {
                        Ty t = expr(*it.expr, true);
                        if (t == Ty::Void) err(it.expr->line, it.expr->col, "cannot print a void value");
                    }
                break;
            case SK::Empty: break;
        }
    }

    void scoped(Stmt& s) {  // a sub-statement gets its own scope
        scopes_.emplace_back();
        stmt(s);
        scopes_.pop_back();
    }

    void cond(Expr& e) {
        Ty t = expr(e);
        if (t != Ty::Error && !numeric(t)) err(e.line, e.col, "condition must be a scalar value");
    }

    Ty target(Expr& e) {
        Symbol* s = lookup(e.name);
        if (!s) { err(e.line, e.col, "use of undeclared identifier '" + e.name + "'"); return Ty::Error; }
        if (s->isConst) err(e.line, e.col, "cannot assign to const variable '" + e.name + "'");
        if (e.kind == EK::Var && s->isArray) err(e.line, e.col, "cannot assign to array '" + e.name + "' as a whole");
        if (e.kind == EK::Index) {
            if (!s->isArray) { err(e.line, e.col, "'" + e.name + "' is not an array"); return Ty::Error; }
            index(e, *s);
        }
        e.uname = s->uname; e.isGlobal = s->global; e.ty = s->ty;
        return s->ty;
    }

    void index(Expr& e, const Symbol& s) {
        Ty it = expr(*e.kids[0]);
        if (it == Ty::Double) err(e.kids[0]->line, e.kids[0]->col, "array index must be an integer");
        auto v = evalConst(*e.kids[0]);
        if (v && v->ty == Ty::Int && s.len > 0 && (v->i < 0 || v->i >= s.len))
            err(e.kids[0]->line, e.kids[0]->col, "array index " + std::to_string(v->i) + " is out of bounds for '" +
                e.name + "' (size " + std::to_string(s.len) + ")");
    }

    // ---------- expressions ----------
    Ty expr(Expr& e, bool allowVoid = false) {
        Ty t = exprInner(e);
        if (t == Ty::Void && !allowVoid) { err(e.line, e.col, "void value used in an expression"); t = Ty::Error; }
        e.ty = t;
        return t;
    }

    Ty exprInner(Expr& e) {
        switch (e.kind) {
            case EK::IntLit: return Ty::Int;
            case EK::FloatLit: return Ty::Double;
            case EK::BoolLit: return Ty::Bool;
            case EK::Var: {
                Symbol* s = lookup(e.name);
                if (!s) {
                    if (res_.funcs.count(e.name)) err(e.line, e.col, "function '" + e.name + "' used without a call");
                    else err(e.line, e.col, "use of undeclared identifier '" + e.name + "'");
                    return Ty::Error;
                }
                if (s->isArray) { err(e.line, e.col, "array '" + e.name + "' must be indexed"); return Ty::Error; }
                e.uname = s->uname; e.isGlobal = s->global;
                if (s->hasConstVal) { e.isConst = true; e.constI = s->ci; e.constD = s->cd; }
                return s->ty;
            }
            case EK::Index: {
                Symbol* s = lookup(e.name);
                if (!s) { err(e.line, e.col, "use of undeclared identifier '" + e.name + "'"); expr(*e.kids[0]); return Ty::Error; }
                if (!s->isArray) { err(e.line, e.col, "'" + e.name + "' is not an array"); expr(*e.kids[0]); return Ty::Error; }
                index(e, *s);
                e.uname = s->uname; e.isGlobal = s->global;
                return s->ty;
            }
            case EK::Unary: {
                Ty t = expr(*e.kids[0]);
                if (t == Ty::Error) return Ty::Error;
                if (!numeric(t)) { err(e.line, e.col, "invalid operand to unary '" + e.op + "'"); return Ty::Error; }
                if (e.op == "!") return Ty::Bool;
                return t == Ty::Double ? Ty::Double : Ty::Int;
            }
            case EK::Binary: {
                Ty a = expr(*e.kids[0]), b = expr(*e.kids[1]);
                if (a == Ty::Error || b == Ty::Error) return Ty::Error;
                if (!numeric(a) || !numeric(b)) { err(e.line, e.col, "invalid operands to '" + e.op + "'"); return Ty::Error; }
                const std::string& op = e.op;
                if (op == "&&" || op == "||" || op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=")
                    return Ty::Bool;
                if (op == "%" && (!integral(a) || !integral(b))) { err(e.line, e.col, "operands of '%' must be integers"); return Ty::Error; }
                if (op == "/" || op == "%") {
                    auto v = evalConst(*e.kids[1]);
                    if (v && v->ty == Ty::Int && v->i == 0) err(e.kids[1]->line, e.kids[1]->col, "division by constant zero");
                }
                return (a == Ty::Double || b == Ty::Double) ? Ty::Double : Ty::Int;
            }
            case EK::Call: {
                auto it = res_.funcs.find(e.name);
                if (it == res_.funcs.end()) {
                    for (auto& k : e.kids) expr(*k);
                    if (lookup(e.name)) err(e.line, e.col, "'" + e.name + "' is not a function");
                    else err(e.line, e.col, "call to undeclared function '" + e.name + "'");
                    return Ty::Error;
                }
                const FuncSig& sig = it->second;
                if (e.kids.size() != sig.params.size())
                    err(e.line, e.col, "function '" + e.name + "' expects " + std::to_string(sig.params.size()) +
                        " argument(s), got " + std::to_string(e.kids.size()));
                for (size_t i = 0; i < e.kids.size(); ++i) {
                    Ty t = expr(*e.kids[i]);
                    if (i < sig.params.size()) checkAssignable(sig.params[i], t, e.kids[i]->line, e.kids[i]->col, "argument " + std::to_string(i + 1));
                }
                return sig.ret;
            }
        }
        return Ty::Error;
    }
};

}  // namespace

SemaResult analyze(Program& p) { return Sema(p).run(); }

// ---------------- AST dump (debugging aid, `--ast`) ----------------
namespace {
void dumpE(const Expr& e, std::string& o) {
    switch (e.kind) {
        case EK::IntLit: o += std::to_string(e.ival); break;
        case EK::BoolLit: o += e.ival ? "true" : "false"; break;
        case EK::FloatLit: o += std::to_string(e.dval); break;
        case EK::Var: o += e.name; break;
        case EK::Index: o += e.name + "["; dumpE(*e.kids[0], o); o += "]"; break;
        case EK::Unary: o += "(" + e.op; dumpE(*e.kids[0], o); o += ")"; break;
        case EK::Binary: o += "("; dumpE(*e.kids[0], o); o += " " + e.op + " "; dumpE(*e.kids[1], o); o += ")"; break;
        case EK::Call:
            o += e.name + "(";
            for (size_t i = 0; i < e.kids.size(); ++i) { if (i) o += ", "; dumpE(*e.kids[i], o); }
            o += ")"; break;
    }
    if (e.ty != Ty::Error) o += std::string(":") + tyName(e.ty);
}
void dumpS(const Stmt& s, int d, std::string& o) {
    std::string ind(d * 2, ' ');
    auto E = [&](const Expr& e) { std::string t; dumpE(e, t); return t; };
    switch (s.kind) {
        case SK::VarDecl:
            o += ind + "Decl " + std::string(s.isConst ? "const " : "") + tyName(s.declTy) + " " + s.name;
            if (s.arraySize) o += "[" + E(*s.arraySize) + "]";
            if (s.init) o += " = " + E(*s.init);
            if (s.hasInitList) o += " = {" + std::to_string(s.initList.size()) + " items}";
            o += "\n"; break;
        case SK::Assign: o += ind + "Assign " + E(*s.target) + " " + s.op + " " + E(*s.value) + "\n"; break;
        case SK::IncDec: o += ind + "IncDec " + E(*s.target) + s.op + "\n"; break;
        case SK::ExprStmt: o += ind + "Call " + E(*s.expr) + "\n"; break;
        case SK::If:
            o += ind + "If " + E(*s.expr) + "\n"; dumpS(*s.thenS, d + 1, o);
            if (s.elseS) { o += ind + "Else\n"; dumpS(*s.elseS, d + 1, o); }
            break;
        case SK::While: o += ind + "While " + E(*s.expr) + "\n"; dumpS(*s.loopBody, d + 1, o); break;
        case SK::For:
            o += ind + "For\n";
            if (s.forInit) { o += ind + " init:\n"; dumpS(*s.forInit, d + 2, o); }
            if (s.expr) o += ind + " cond: " + E(*s.expr) + "\n";
            if (s.forStep) { o += ind + " step:\n"; dumpS(*s.forStep, d + 2, o); }
            dumpS(*s.loopBody, d + 1, o); break;
        case SK::Return: o += ind + "Return" + (s.expr ? " " + E(*s.expr) : "") + "\n"; break;
        case SK::Block: o += ind + "Block\n"; for (auto& x : s.body) dumpS(*x, d + 1, o); break;
        case SK::Print:
            o += ind + "Print";
            for (auto& it : s.items)
                if (it.kind == PrintItem::Value) o += " " + E(*it.expr);
                else if (it.kind == PrintItem::Endl) o += " endl";
                else {
                    std::string q;
                    for (char ch : it.str) q += ch == '\n' ? "\\n" : ch == '\t' ? "\\t" : std::string(1, ch);
                    o += " \"" + q + "\"";
                }
            o += "\n"; break;
        case SK::Empty: o += ind + "Empty\n"; break;
    }
}
}  // namespace

std::string dumpAst(const Program& p) {
    std::string o = "Program\n";
    for (auto& g : p.globals) dumpS(*g, 1, o);
    for (auto& f : p.funcs) {
        o += "  Func " + std::string(tyName(f->ret)) + " " + f->name + "(";
        for (size_t i = 0; i < f->params.size(); ++i) { if (i) o += ", "; o += std::string(tyName(f->params[i].ty)) + " " + f->params[i].name; }
        o += ")\n";
        dumpS(*f->body, 2, o);
    }
    return o;
}

}  // namespace ww
