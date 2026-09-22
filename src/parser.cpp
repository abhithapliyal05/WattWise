// parser.cpp — recursive-descent parser for the WattWise C++ subset.
//
// Expression precedence (lowest → highest), matching C++:
//   ||   &&   == !=   < <= > >=   + -   * / %   unary(- ! +)   postfix/primary
// `<<` appears only inside `cout << ...` chains, where each item is parsed at
// additive level — exactly as C++ binds it.
#include "wattwise/parser.hpp"
#include <set>

namespace ww {
namespace {

struct ParseAbort {};  // internal: unwinds to the nearest recovery point

// C++ keywords/names that are deliberately outside the subset: reported with
// a clear message instead of a generic "unexpected token".
const std::set<std::string> kUnsupported = {
    "class", "struct", "union", "enum", "template", "typename", "new", "delete",
    "switch", "case", "default", "do", "break", "continue", "goto", "auto",
    "char", "float", "long", "short", "unsigned", "signed", "static", "extern",
    "sizeof", "try", "catch", "throw", "operator", "this", "virtual", "public",
    "private", "protected", "friend", "inline", "cin", "string", "vector", "nullptr"};

class Parser {
public:
    explicit Parser(const std::vector<Token>& t) : t_(t) {}

    Program run() {
        Program p;
        while (!at(Tok::End)) {
            try { topLevel(p); }
            catch (ParseAbort&) { syncTop(); }
        }
        if (!errs_.empty()) throw CompileError("syntax", errs_);
        return p;
    }

private:
    const std::vector<Token>& t_;
    size_t i_ = 0;
    std::vector<Diag> errs_;

    // ---------- token helpers ----------
    const Token& cur() const { return t_[i_]; }
    const Token& ahead(size_t k) const { return t_[std::min(i_ + k, t_.size() - 1)]; }
    bool at(Tok k) const { return cur().kind == k; }
    bool accept(Tok k) { if (at(k)) { i_++; return true; } return false; }
    const Token& expect(Tok k, const char* what) {
        if (!at(k)) fail(std::string("expected ") + tokName(k) + " " + what + ", found " + describe(cur()));
        return t_[i_++];
    }
    static std::string describe(const Token& t) {
        if (t.kind == Tok::Ident || t.kind == Tok::IntLit || t.kind == Tok::FloatLit) return "'" + t.text + "'";
        return tokName(t.kind);
    }
    [[noreturn]] void fail(const std::string& m) { failAt(cur(), m); }
    [[noreturn]] void failAt(const Token& t, const std::string& m) {
        errs_.push_back({t.line, t.col, m, false});
        throw ParseAbort{};
    }
    void checkUnsupported() {
        if (at(Tok::Ident) && kUnsupported.count(cur().text))
            fail("C++ feature '" + cur().text + "' is outside the WattWise subset");
    }

    // Recovery: skip to just after the next ';' or to a '}'.
    void syncStmt() {
        while (!at(Tok::End) && !at(Tok::Semi) && !at(Tok::RBrace) && !at(Tok::LBrace)) i_++;
        accept(Tok::Semi);
    }
    void syncTop() {
        int depth = 0;
        while (!at(Tok::End)) {
            if (at(Tok::LBrace)) depth++;
            if (at(Tok::RBrace)) { depth--; i_++; if (depth <= 0) return; continue; }
            if (at(Tok::Semi) && depth == 0) { i_++; return; }
            i_++;
        }
    }

    bool atType() const {
        return at(Tok::KwInt) || at(Tok::KwDouble) || at(Tok::KwBool) || at(Tok::KwVoid) || at(Tok::KwConst);
    }
    Ty parseType() {
        switch (cur().kind) {
            case Tok::KwInt: i_++; return Ty::Int;
            case Tok::KwDouble: i_++; return Ty::Double;
            case Tok::KwBool: i_++; return Ty::Bool;
            case Tok::KwVoid: i_++; return Ty::Void;
            default: checkUnsupported(); fail("expected a type, found " + describe(cur()));
        }
    }

    // ---------- top level ----------
    void topLevel(Program& p) {
        if (accept(Tok::KwUsing)) {  // using namespace std;
            expect(Tok::KwNamespace, "after 'using'");
            const Token& n = expect(Tok::Ident, "after 'using namespace'");
            if (n.text != "std") failAt(n, "only 'using namespace std;' is supported");
            expect(Tok::Semi, "after using-directive");
            return;
        }
        checkUnsupported();
        if (!atType()) fail("expected a declaration or function definition, found " + describe(cur()));
        // function definition: type ident '('
        size_t k = at(Tok::KwConst) ? 1 : 0;
        if (ahead(k + 1).kind == Tok::Ident && ahead(k + 2).kind == Tok::LParen && k == 0) {
            p.order.push_back({true, p.funcs.size()});
            p.funcs.push_back(funcDef());
            return;
        }
        auto decls = varDeclList();
        expect(Tok::Semi, "after declaration");
        for (auto& d : decls) {
            p.order.push_back({false, p.globals.size()});
            p.globals.push_back(std::move(d));
        }
    }

    std::unique_ptr<FuncDecl> funcDef() {
        auto f = std::make_unique<FuncDecl>();
        f->line = cur().line; f->col = cur().col;
        f->ret = parseType();
        f->name = expect(Tok::Ident, "as function name").text;
        expect(Tok::LParen, "after function name");
        if (at(Tok::KwVoid) && ahead(1).kind == Tok::RParen) i_++;  // f(void)
        if (!at(Tok::RParen)) {
            do {
                Param pr; pr.line = cur().line; pr.col = cur().col;
                pr.ty = parseType();
                pr.name = expect(Tok::Ident, "as parameter name").text;
                if (at(Tok::LBracket)) fail("array parameters are not in the WattWise subset (use a global array)");
                f->params.push_back(pr);
            } while (accept(Tok::Comma));
        }
        expect(Tok::RParen, "after parameter list");
        if (!at(Tok::LBrace)) fail("expected '{' to begin the function body (prototypes are not supported)");
        f->body = block();
        return f;
    }

    // type declarator (',' declarator)*   — used for globals, locals and for-init
    std::vector<StmtP> varDeclList() {
        std::vector<StmtP> out;
        bool isConst = accept(Tok::KwConst);
        int tl = cur().line, tc = cur().col;
        Ty ty = parseType();
        if (ty == Ty::Void) { errs_.push_back({tl, tc, "variable declared void", false}); throw ParseAbort{}; }
        do {
            auto s = std::make_unique<Stmt>();
            s->kind = SK::VarDecl;
            s->declTy = ty; s->isConst = isConst;
            const Token& n = expect(Tok::Ident, "as variable name");
            s->name = n.text; s->line = n.line; s->col = n.col;
            if (accept(Tok::LBracket)) {
                s->arraySize = expr();
                expect(Tok::RBracket, "after array size");
                if (at(Tok::LBracket)) fail("multi-dimensional arrays are not in the WattWise subset");
            }
            if (accept(Tok::Assign)) {
                if (at(Tok::LBrace)) {
                    i_++;
                    s->hasInitList = true;
                    if (!at(Tok::RBrace)) {
                        do { s->initList.push_back(expr()); } while (accept(Tok::Comma) && !at(Tok::RBrace));
                    }
                    expect(Tok::RBrace, "to close initializer list");
                } else {
                    s->init = expr();
                }
            }
            out.push_back(std::move(s));
        } while (accept(Tok::Comma));
        return out;
    }

    // ---------- statements ----------
    StmtP block() {
        auto b = std::make_unique<Stmt>();
        b->kind = SK::Block; b->line = cur().line; b->col = cur().col;
        expect(Tok::LBrace, "to begin block");
        while (!at(Tok::RBrace) && !at(Tok::End)) {
            try { stmtInto(b->body); }
            catch (ParseAbort&) { syncStmt(); }
        }
        expect(Tok::RBrace, "to close block");
        return b;
    }

    // Parses one statement; declarations with several declarators add several.
    void stmtInto(std::vector<StmtP>& out) {
        if (atType()) {
            auto ds = varDeclList();
            expect(Tok::Semi, "after declaration");
            for (auto& d : ds) out.push_back(std::move(d));
            return;
        }
        out.push_back(stmt());
    }

    StmtP single() {  // a statement used as if/while/for body
        if (atType()) {
            // `if (c) int x = 1;` — legal C++ but pointless; wrap in a block
            auto b = std::make_unique<Stmt>();
            b->kind = SK::Block; b->line = cur().line; b->col = cur().col;
            stmtInto(b->body);
            return b;
        }
        return stmt();
    }

    StmtP stmt() {
        checkUnsupported();
        const Token& st = cur();
        auto mk = [&](SK k) { auto s = std::make_unique<Stmt>(); s->kind = k; s->line = st.line; s->col = st.col; return s; };

        if (at(Tok::LBrace)) return block();
        if (accept(Tok::Semi)) return mk(SK::Empty);

        if (accept(Tok::KwIf)) {
            auto s = mk(SK::If);
            expect(Tok::LParen, "after 'if'");
            s->expr = expr();
            expect(Tok::RParen, "after if-condition");
            s->thenS = single();
            if (accept(Tok::KwElse)) s->elseS = single();
            return s;
        }
        if (accept(Tok::KwWhile)) {
            auto s = mk(SK::While);
            expect(Tok::LParen, "after 'while'");
            s->expr = expr();
            expect(Tok::RParen, "after while-condition");
            s->loopBody = single();
            return s;
        }
        if (accept(Tok::KwFor)) {
            auto s = mk(SK::For);
            expect(Tok::LParen, "after 'for'");
            if (!at(Tok::Semi)) {
                if (atType()) {
                    auto ds = varDeclList();
                    if (ds.size() != 1) fail("only one declaration is allowed in a for-initializer");
                    s->forInit = std::move(ds[0]);
                } else {
                    s->forInit = simple();
                }
            }
            expect(Tok::Semi, "after for-initializer");
            if (!at(Tok::Semi)) s->expr = expr();
            expect(Tok::Semi, "after for-condition");
            if (!at(Tok::RParen)) s->forStep = simple();
            expect(Tok::RParen, "to close for-header");
            s->loopBody = single();
            return s;
        }
        if (accept(Tok::KwReturn)) {
            auto s = mk(SK::Return);
            if (!at(Tok::Semi)) s->expr = expr();
            expect(Tok::Semi, "after return");
            return s;
        }
        if (isCout()) return printStmt();

        auto s = simple();
        expect(Tok::Semi, "after statement");
        return s;
    }

    bool isCout() const {
        if (at(Tok::Ident) && cur().text == "cout") return true;
        return at(Tok::Ident) && cur().text == "std" && ahead(1).kind == Tok::ColonColon &&
               ahead(2).kind == Tok::Ident && ahead(2).text == "cout";
    }
    bool isEndl() const {
        if (at(Tok::Ident) && cur().text == "endl") return true;
        return at(Tok::Ident) && cur().text == "std" && ahead(1).kind == Tok::ColonColon &&
               ahead(2).kind == Tok::Ident && ahead(2).text == "endl";
    }

    StmtP printStmt() {
        auto s = std::make_unique<Stmt>();
        s->kind = SK::Print; s->line = cur().line; s->col = cur().col;
        if (cur().text == "std") i_ += 2;
        i_++;  // cout
        if (!at(Tok::Shl)) fail("expected '<<' after cout");
        while (accept(Tok::Shl)) {
            PrintItem it;
            if (at(Tok::StrLit) || at(Tok::CharLit)) { it.kind = PrintItem::Str; it.str = cur().text; i_++; }
            else if (isEndl()) { if (cur().text == "std") i_ += 2; i_++; it.kind = PrintItem::Endl; }
            else { it.kind = PrintItem::Value; it.expr = additive(); }
            s->items.push_back(std::move(it));
        }
        expect(Tok::Semi, "after cout statement");
        return s;
    }

    // assignment, compound assignment, ++/--, or a call
    StmtP simple() {
        auto s = std::make_unique<Stmt>();
        s->line = cur().line; s->col = cur().col;
        if (at(Tok::PlusPlus) || at(Tok::MinusMinus)) {  // ++x
            s->kind = SK::IncDec;
            s->op = cur().text; i_++;
            s->target = lvalue();
            return s;
        }
        if (!at(Tok::Ident)) fail("expected a statement, found " + describe(cur()));
        if (ahead(1).kind == Tok::LParen) {
            s->kind = SK::ExprStmt;
            s->expr = postfix();
            return s;
        }
        s->target = lvalue();
        switch (cur().kind) {
            case Tok::Assign: case Tok::PlusAssign: case Tok::MinusAssign:
            case Tok::StarAssign: case Tok::SlashAssign: case Tok::PercentAssign:
                s->kind = SK::Assign; s->op = cur().text; i_++;
                s->value = expr();
                return s;
            case Tok::PlusPlus: case Tok::MinusMinus:
                s->kind = SK::IncDec; s->op = cur().text; i_++;
                return s;
            default:
                fail("expected assignment operator after '" + s->target->name + "', found " + describe(cur()));
        }
    }

    ExprP lvalue() {
        checkUnsupported();
        const Token& n = expect(Tok::Ident, "as assignment target");
        auto e = std::make_unique<Expr>();
        e->line = n.line; e->col = n.col; e->name = n.text;
        if (accept(Tok::LBracket)) {
            e->kind = EK::Index;
            e->kids.push_back(expr());
            expect(Tok::RBracket, "after index");
        } else {
            e->kind = EK::Var;
        }
        return e;
    }

    // ---------- expressions ----------
    ExprP bin(const std::string& op, ExprP l, ExprP r, const Token& at) {
        auto e = std::make_unique<Expr>();
        e->kind = EK::Binary; e->op = op; e->line = at.line; e->col = at.col;
        e->kids.push_back(std::move(l)); e->kids.push_back(std::move(r));
        return e;
    }
    ExprP expr() { return orExpr(); }
    ExprP orExpr() {
        auto l = andExpr();
        while (at(Tok::OrOr)) { const Token& o = t_[i_++]; l = bin("||", std::move(l), andExpr(), o); }
        return l;
    }
    ExprP andExpr() {
        auto l = equality();
        while (at(Tok::AndAnd)) { const Token& o = t_[i_++]; l = bin("&&", std::move(l), equality(), o); }
        return l;
    }
    ExprP equality() {
        auto l = relational();
        while (at(Tok::Eq) || at(Tok::Ne)) { const Token& o = t_[i_++]; l = bin(o.text, std::move(l), relational(), o); }
        return l;
    }
    ExprP relational() {
        auto l = additive();
        while (at(Tok::Lt) || at(Tok::Le) || at(Tok::Gt) || at(Tok::Ge)) {
            const Token& o = t_[i_++]; l = bin(o.text, std::move(l), additive(), o);
        }
        if (at(Tok::Shl)) fail("'<<' is only allowed in cout statements (shift operators are not in the subset)");
        return l;
    }
    ExprP additive() {
        auto l = multiplicative();
        while (at(Tok::Plus) || at(Tok::Minus)) { const Token& o = t_[i_++]; l = bin(o.text, std::move(l), multiplicative(), o); }
        return l;
    }
    ExprP multiplicative() {
        auto l = unary();
        while (at(Tok::Star) || at(Tok::Slash) || at(Tok::Percent)) {
            const Token& o = t_[i_++]; l = bin(o.text, std::move(l), unary(), o);
        }
        return l;
    }
    ExprP unary() {
        if (at(Tok::Minus) || at(Tok::Not) || at(Tok::Plus)) {
            const Token& o = t_[i_++];
            auto operand = unary();
            if (o.kind == Tok::Plus) return operand;
            auto e = std::make_unique<Expr>();
            e->kind = EK::Unary; e->op = o.text; e->line = o.line; e->col = o.col;
            e->kids.push_back(std::move(operand));
            return e;
        }
        if (at(Tok::PlusPlus) || at(Tok::MinusMinus))
            fail("'++'/'--' inside expressions is not in the subset (use it as a statement)");
        return postfix();
    }
    ExprP postfix() {
        checkUnsupported();
        const Token& t = cur();
        auto e = std::make_unique<Expr>();
        e->line = t.line; e->col = t.col;
        switch (t.kind) {
            case Tok::IntLit: i_++; e->kind = EK::IntLit; e->ival = t.ival; return e;
            case Tok::FloatLit: i_++; e->kind = EK::FloatLit; e->dval = t.dval; return e;
            case Tok::KwTrue: i_++; e->kind = EK::BoolLit; e->ival = 1; return e;
            case Tok::KwFalse: i_++; e->kind = EK::BoolLit; e->ival = 0; return e;
            case Tok::LParen: {
                i_++;
                auto inner = expr();
                expect(Tok::RParen, "to close parenthesised expression");
                return inner;
            }
            case Tok::Ident: {
                i_++;
                e->name = t.text;
                if (accept(Tok::LParen)) {
                    e->kind = EK::Call;
                    if (!at(Tok::RParen)) { do { e->kids.push_back(expr()); } while (accept(Tok::Comma)); }
                    expect(Tok::RParen, "to close argument list");
                } else if (accept(Tok::LBracket)) {
                    e->kind = EK::Index;
                    e->kids.push_back(expr());
                    expect(Tok::RBracket, "after index");
                } else {
                    e->kind = EK::Var;
                }
                if (at(Tok::PlusPlus) || at(Tok::MinusMinus))
                    fail("'++'/'--' inside expressions is not in the subset (use it as a statement)");
                return e;
            }
            case Tok::StrLit: case Tok::CharLit:
                fail("string/char literals are only allowed in cout statements");
            default:
                fail("expected an expression, found " + describe(t));
        }
    }
};

}  // namespace

Program parse(const std::vector<Token>& toks) { return Parser(toks).run(); }

}  // namespace ww
