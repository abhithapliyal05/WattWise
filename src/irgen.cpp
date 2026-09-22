// irgen.cpp — lowers the annotated AST to quadruples.
//
// Key translation schemes (Dragon Book §6.4-6.7):
//   E1 op E2      t = E1.addr op E2.addr            (with i2d when types mix)
//   a[i]          t = a[i]                          (explicit Load: memory op)
//   E1 && E2      t = 0; if !E1 goto L; t = (E2 != 0); L:      (short circuit)
//   while (B) S   Lc: B; iffalse B goto Le; S; goto Lc; Le:
//   for (I;B;U) S I; Lc: B; iffalse B goto Le; S; U; goto Lc; Le:
// Assignments retarget the last instruction when it produced a fresh temp,
// so `x = a + b` is one quad, not "t = a + b; x = t".
#include "wattwise/irgen.hpp"
#include <map>

namespace ww {
namespace {

Ty irTy(Ty t) { return t == Ty::Double ? Ty::Double : Ty::Int; }

class Gen {
public:
    Gen(const Program& p, const SemaResult& s) : prog_(p), sema_(s) {}

    IRProgram run() {
        for (auto& g : prog_.globals) global(*g);
        for (auto& f : prog_.funcs) func(*f);
        return std::move(ir_);
    }

private:
    const Program& prog_;
    const SemaResult& sema_;
    IRProgram ir_;
    IRFunc* f_ = nullptr;
    int line_ = 0;

    void emit(Quad q) { q.line = line_; f_->code.push_back(std::move(q)); }
    Quad Q(Op op, Operand dst = {}, Operand a = {}, Operand b = {}, Operand c = {}) {
        Quad q; q.op = op; q.dst = dst; q.a = a; q.b = b; q.c = c; return q;
    }
    void label(const Operand& l) { emit(Q(Op::Label, l)); }

    // ---------- constants for globals ----------
    static double constOf(const Expr& e) {
        switch (e.kind) {
            case EK::IntLit: case EK::BoolLit: return (double)e.ival;
            case EK::FloatLit: return e.dval;
            case EK::Var: return e.ty == Ty::Double ? e.constD : (double)e.constI;
            case EK::Unary: return e.op == "-" ? -constOf(*e.kids[0]) : (constOf(*e.kids[0]) == 0);
            case EK::Binary: {
                double a = constOf(*e.kids[0]), b = constOf(*e.kids[1]);
                bool ints = e.ty != Ty::Double;
                if (e.op == "+") return a + b;
                if (e.op == "-") return a - b;
                if (e.op == "*") return a * b;
                if (e.op == "/") return ints ? (double)((long long)a / (long long)b) : a / b;
                if (e.op == "%") return (double)((long long)a % (long long)b);
                return 0;
            }
            default: return 0;
        }
    }

    void global(const Stmt& d) {
        if (d.arraySize) {
            ArrayInfo ai; ai.ty = irTy(d.declTy); ai.len = d.arrayLen;
            ai.init.assign(d.arrayLen, 0.0);
            for (size_t i = 0; i < d.initList.size(); ++i) {
                double v = constOf(*d.initList[i]);
                ai.init[i] = ai.ty == Ty::Int ? (double)(long long)v : v;
            }
            ir_.globalArrays[d.uname] = ai;
        } else {
            double v = d.init ? constOf(*d.init) : 0.0;
            if (d.declTy == Ty::Double) ir_.globalScalars[d.uname] = Operand::cd(v);
            else if (d.declTy == Ty::Bool) ir_.globalScalars[d.uname] = Operand::ci(v != 0);
            else ir_.globalScalars[d.uname] = Operand::ci((long long)v);
        }
    }

    // ---------- functions ----------
    void func(const FuncDecl& fd) {
        ir_.funcs.emplace_back();
        f_ = &ir_.funcs.back();
        f_->name = fd.name;
        f_->ret = fd.ret == Ty::Void ? Ty::Void : irTy(fd.ret);
        for (auto& p : fd.params) f_->params.push_back(Operand::var(p.uname, irTy(p.ty), false));
        line_ = fd.line;
        for (auto& s : fd.body->body) stmt(*s);
        if (f_->code.empty() || f_->code.back().op != Op::Ret) {
            if (fd.ret == Ty::Void) emit(Q(Op::Ret));
            else emit(Q(Op::Ret, {}, fd.ret == Ty::Double ? Operand::cd(0) : Operand::ci(0)));
        }
    }

    // ---------- conversions ----------
    // Converts `v` (of source-language type `from`) to type `to`.
    Operand convert(Operand v, Ty from, Ty to) {
        if (to == Ty::Bool && from != Ty::Bool) {       // normalise to 0/1
            if (v.k == OK::ConstI) return Operand::ci(v.ival != 0);
            if (v.k == OK::ConstD) return Operand::ci(v.dval != 0);
            Operand t = f_->newTemp(Ty::Int);
            emit(Q(Op::Ne, t, v, v.ty == Ty::Double ? Operand::cd(0) : Operand::ci(0)));
            return t;
        }
        Ty want = irTy(to);
        if (v.ty == want) return v;
        if (v.k == OK::ConstI) return Operand::cd((double)v.ival);
        if (v.k == OK::ConstD) return Operand::ci((long long)v.dval);
        Operand t = f_->newTemp(want);
        emit(Q(want == Ty::Double ? Op::I2D : Op::D2I, t, v));
        return t;
    }

    // ---------- expressions ----------
    Operand varOp(const Expr& e) { return Operand::var(e.uname, irTy(e.ty), e.isGlobal); }

    Operand expr(const Expr& e) {
        switch (e.kind) {
            case EK::IntLit: case EK::BoolLit: return Operand::ci(e.ival);
            case EK::FloatLit: return Operand::cd(e.dval);
            case EK::Var:
                if (e.isConst) return e.ty == Ty::Double ? Operand::cd(e.constD) : Operand::ci(e.constI);
                return varOp(e);
            case EK::Index: {
                Operand idx = convert(expr(*e.kids[0]), e.kids[0]->ty, Ty::Int);
                Operand t = f_->newTemp(irTy(e.ty));
                emit(Q(Op::Load, t, varOp(e), idx));
                return t;
            }
            case EK::Unary: {
                Operand v = expr(*e.kids[0]);
                if (e.op == "!") {
                    Operand t = f_->newTemp(Ty::Int);
                    emit(Q(Op::Not, t, v));
                    return t;
                }
                v = convert(v, e.kids[0]->ty, e.ty);
                Operand t = f_->newTemp(irTy(e.ty));
                emit(Q(Op::Neg, t, v));
                return t;
            }
            case EK::Binary: return binary(e);
            case EK::Call: return call(e);
        }
        return {};
    }

    static Op binOp(const std::string& s) {
        static const std::map<std::string, Op> ops = {
            {"+", Op::Add}, {"-", Op::Sub}, {"*", Op::Mul}, {"/", Op::Div}, {"%", Op::Mod},
            {"<", Op::Lt}, {"<=", Op::Le}, {">", Op::Gt}, {">=", Op::Ge}, {"==", Op::Eq}, {"!=", Op::Ne}};
        return ops.at(s);
    }

    Operand binary(const Expr& e) {
        const Expr& L = *e.kids[0];
        const Expr& R = *e.kids[1];
        if (e.op == "&&" || e.op == "||") {
            // result temp is assigned on two paths (the only multi-def temps)
            bool isAnd = e.op == "&&";
            Operand t = f_->newTemp(Ty::Int);
            Operand lEnd = f_->newLabel();
            emit(Q(Op::Copy, t, Operand::ci(isAnd ? 0 : 1)));
            Operand l = expr(L);
            if (isAnd) {
                emit(Q(Op::JmpF, lEnd, l));
            } else {
                Operand lRhs = f_->newLabel();
                emit(Q(Op::JmpF, lRhs, l));
                emit(Q(Op::Jmp, lEnd));
                label(lRhs);
            }
            Operand r = expr(R);
            emit(Q(Op::Ne, t, r, r.ty == Ty::Double ? Operand::cd(0) : Operand::ci(0)));
            label(lEnd);
            return t;
        }
        Operand a = expr(L), b = expr(R);
        Ty common = (irTy(L.ty) == Ty::Double || irTy(R.ty) == Ty::Double) ? Ty::Double : Ty::Int;
        a = convert(a, L.ty == Ty::Bool ? Ty::Int : L.ty, common);
        b = convert(b, R.ty == Ty::Bool ? Ty::Int : R.ty, common);
        Op op = binOp(e.op);
        bool cmp = op >= Op::Lt && op <= Op::Ne;
        Operand t = f_->newTemp(cmp ? Ty::Int : common);
        emit(Q(op, t, a, b));
        return t;
    }

    Operand call(const Expr& e) {
        const FuncSig& sig = sema_.funcs.at(e.name);
        std::vector<Operand> args;
        for (size_t i = 0; i < e.kids.size(); ++i)
            args.push_back(convert(expr(*e.kids[i]), e.kids[i]->ty, sig.params[i]));
        for (auto& a : args) emit(Q(Op::Param, {}, a));   // after evaluating all args
        Operand fn; fn.k = OK::Func; fn.name = e.name;
        Operand n = Operand::ci((long long)args.size());
        Operand dst;
        if (sig.ret != Ty::Void) dst = f_->newTemp(irTy(sig.ret));
        emit(Q(Op::Call, dst, fn, n));
        return dst;
    }

    // Store `v` into scalar var `x`: retarget the defining quad when possible.
    void assignVar(const Operand& x, Operand v) {
        if (v.k == OK::Temp && !f_->code.empty()) {
            Quad& last = f_->code.back();
            const Operand* d = defOf(last);
            if (d && *d == v && last.op != Op::Label) { last.dst = x; return; }
        }
        emit(Q(Op::Copy, x, v));
    }

    // ---------- statements ----------
    void stmt(const Stmt& s) {
        line_ = s.line;
        switch (s.kind) {
            case SK::VarDecl: decl(s); break;
            case SK::Block: for (auto& x : s.body) stmt(*x); break;
            case SK::Empty: break;
            case SK::ExprStmt: expr(*s.expr); break;
            case SK::Assign: assign(s); break;
            case SK::IncDec: incdec(s); break;
            case SK::If: {
                Operand c = expr(*s.expr);
                Operand lElse = f_->newLabel();
                emit(Q(Op::JmpF, lElse, c));
                stmt(*s.thenS);
                if (s.elseS) {
                    Operand lEnd = f_->newLabel();
                    line_ = s.line;
                    emit(Q(Op::Jmp, lEnd));
                    label(lElse);
                    stmt(*s.elseS);
                    label(lEnd);
                } else {
                    label(lElse);
                }
                break;
            }
            case SK::While: {
                Operand lc = f_->newLabel(), le = f_->newLabel();
                label(lc);
                Operand c = expr(*s.expr);
                emit(Q(Op::JmpF, le, c));
                stmt(*s.loopBody);
                line_ = s.line;
                emit(Q(Op::Jmp, lc));
                label(le);
                break;
            }
            case SK::For: {
                if (s.forInit) stmt(*s.forInit);
                line_ = s.line;
                Operand lc = f_->newLabel(), le = f_->newLabel();
                label(lc);
                if (s.expr) {
                    Operand c = expr(*s.expr);
                    emit(Q(Op::JmpF, le, c));
                }
                stmt(*s.loopBody);
                if (s.forStep) stmt(*s.forStep);
                line_ = s.line;
                emit(Q(Op::Jmp, lc));
                label(le);
                break;
            }
            case SK::Return: {
                if (!s.expr) { emit(Q(Op::Ret)); break; }
                Operand v = expr(*s.expr);
                const Ty rt = sema_.funcs.at(f_->name).ret;
                emit(Q(Op::Ret, {}, convert(v, s.expr->ty, rt)));
                break;
            }
            case SK::Print:
                for (auto& it : s.items) {
                    if (it.kind == PrintItem::Endl) emit(Q(Op::PrintNL));
                    else if (it.kind == PrintItem::Str) { Operand o; o.k = OK::Str; o.name = it.str; emit(Q(Op::PrintS, {}, o)); }
                    else emit(Q(Op::Print, {}, expr(*it.expr)));
                }
                break;
        }
    }

    void decl(const Stmt& s) {
        if (s.arraySize) {
            ArrayInfo ai; ai.ty = irTy(s.declTy); ai.len = s.arrayLen;
            f_->arrays[s.uname] = ai;
            if (!s.hasInitList) return;   // C++ leaves it indeterminate; we don't initialise
            Operand arr = Operand::var(s.uname, ai.ty, false);
            Operand zero = ai.ty == Ty::Double ? Operand::cd(0) : Operand::ci(0);
            for (size_t i = 0; i < s.initList.size(); ++i)
                emit(Q(Op::Store, {}, arr, Operand::ci((long long)i), convert(expr(*s.initList[i]), s.initList[i]->ty, s.declTy)));
            long long from = (long long)s.initList.size();
            if (from >= s.arrayLen) return;
            if (s.arrayLen - from <= 8) {                // short tail: unrolled zero stores
                for (long long i = from; i < s.arrayLen; ++i) emit(Q(Op::Store, {}, arr, Operand::ci(i), zero));
                return;
            }
            // long tail: a zero-fill loop (what memset-style initialisation costs)
            Operand it = f_->newTemp(Ty::Int), c = f_->newTemp(Ty::Int);
            Operand lc = f_->newLabel(), le = f_->newLabel();
            emit(Q(Op::Copy, it, Operand::ci(from)));
            label(lc);
            emit(Q(Op::Lt, c, it, Operand::ci(s.arrayLen)));
            emit(Q(Op::JmpF, le, c));
            emit(Q(Op::Store, {}, arr, it, zero));
            emit(Q(Op::Add, it, it, Operand::ci(1)));
            emit(Q(Op::Jmp, lc));
            label(le);
            return;
        }
        if (s.isConst && !s.init) return;
        if (!s.init) return;
        if (s.isConst && (s.init->kind == EK::IntLit || s.init->kind == EK::FloatLit)) return;  // folded at every use
        Operand x = Operand::var(s.uname, irTy(s.declTy), false);
        assignVar(x, convert(expr(*s.init), s.init->ty, s.declTy));
    }

    void assign(const Stmt& s) {
        const Expr& tgt = *s.target;
        Ty tt = tgt.ty;
        if (s.op == "=") {
            if (tgt.kind == EK::Var) {
                assignVar(varOp(tgt), convert(expr(*s.value), s.value->ty, tt));
            } else {
                Operand idx = convert(expr(*tgt.kids[0]), tgt.kids[0]->ty, Ty::Int);
                Operand v = convert(expr(*s.value), s.value->ty, tt);
                emit(Q(Op::Store, {}, varOp(tgt), idx, v));
            }
            return;
        }
        // compound: x op= e  ==>  x = x op e
        std::string bop = s.op.substr(0, 1);
        Op op = binOp(bop);
        Ty vt = s.value->ty;
        Ty common = (irTy(tt) == Ty::Double || irTy(vt) == Ty::Double) ? Ty::Double : Ty::Int;
        if (tgt.kind == EK::Var) {
            Operand x = varOp(tgt);
            Operand v = convert(expr(*s.value), vt == Ty::Bool ? Ty::Int : vt, common);
            Operand xv = convert(x, tt == Ty::Bool ? Ty::Int : tt, common);
            Operand t = f_->newTemp(common);
            emit(Q(op, t, xv, v));
            assignVar(x, convert(t, common, tt));
        } else {
            Operand idx = convert(expr(*tgt.kids[0]), tgt.kids[0]->ty, Ty::Int);
            Operand old = f_->newTemp(irTy(tt));
            emit(Q(Op::Load, old, varOp(tgt), idx));
            Operand v = convert(expr(*s.value), vt == Ty::Bool ? Ty::Int : vt, common);
            Operand ov = convert(old, tt, common);
            Operand t = f_->newTemp(common);
            emit(Q(op, t, ov, v));
            emit(Q(Op::Store, {}, varOp(tgt), idx, convert(t, common, tt)));
        }
    }

    void incdec(const Stmt& s) {
        const Expr& tgt = *s.target;
        Op op = s.op == "++" ? Op::Add : Op::Sub;
        Ty t = irTy(tgt.ty);
        Operand one = t == Ty::Double ? Operand::cd(1) : Operand::ci(1);
        if (tgt.kind == EK::Var) {
            Operand x = varOp(tgt);
            emit(Q(op, x, x, one));
        } else {
            Operand idx = convert(expr(*tgt.kids[0]), tgt.kids[0]->ty, Ty::Int);
            Operand old = f_->newTemp(t), nv = f_->newTemp(t);
            emit(Q(Op::Load, old, varOp(tgt), idx));
            emit(Q(op, nv, old, one));
            emit(Q(Op::Store, {}, varOp(tgt), idx, nv));
        }
    }
};

}  // namespace

IRProgram generateIR(const Program& p, const SemaResult& sema) { return Gen(p, sema).run(); }

}  // namespace ww
