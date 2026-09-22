// interpreter.cpp — a register-slot interpreter for the WattWise IR.
//
// Before running, every operand is resolved to a constant, a local slot or a
// global slot, and every label to a quad index, so the inner loop is a plain
// switch without string lookups. Calls use an explicit frame stack (no host
// recursion), so deep recursion in the guest is reported, not crashed on.
// Integer semantics are 32-bit two's complement (what g++ produces for the
// well-defined programs the subset admits).
#include "wattwise/interpreter.hpp"
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace ww {
namespace {

struct Val { long long i = 0; double d = 0; };

inline long long wrap32(long long v) { return static_cast<long long>(static_cast<int32_t>(static_cast<uint32_t>(v))); }

struct POp {
    enum Kind : uint8_t { None, Const, Local, Global } kind = None;
    bool fp = false;
    int slot = -1;
    Val c;
};

struct PQuad {
    Op op;
    POp dst, a, b, c;
    int target = -1;     // resolved jump target / callee index
    int arr = -1;        // array slot (local or global, see arrGlobal)
    bool arrGlobal = false;
    long long nargs = 0;
    std::string str;     // PrintS payload
};

struct PFunc {
    std::string name;
    std::vector<PQuad> code;
    int nSlots = 0;
    std::vector<int> paramSlots;
    std::vector<std::pair<long long, bool>> arrays;   // len, fp (per local array slot)
};

struct Frame {
    const PFunc* fn;
    size_t pc = 0;
    std::vector<Val> slots;
    std::vector<std::vector<Val>> arrays;
    POp retDst;          // where the caller wants the return value
    std::vector<uint64_t>* counts;
};

class Machine {
public:
    Machine(const IRProgram& p, const RunLimits& l) : prog_(p), lim_(l) {}

    RunResult run() {
        prepare();
        auto it = funcIndex_.find("main");
        if (it == funcIndex_.end()) { res_.ok = false; res_.error = "no main function"; return res_; }
        exec(it->second);
        res_.output = out_.str();
        return res_;
    }

private:
    const IRProgram& prog_;
    RunLimits lim_;
    RunResult res_;
    std::ostringstream out_;
    std::vector<PFunc> funcs_;
    std::unordered_map<std::string, int> funcIndex_;
    std::vector<Val> gslots_;
    std::unordered_map<std::string, int> gslotIndex_;
    std::vector<std::vector<Val>> garrays_;
    std::vector<bool> garrayFp_;
    std::unordered_map<std::string, int> garrIndex_;

    void prepare() {
        for (auto& [n, v] : prog_.globalScalars) {
            gslotIndex_[n] = (int)gslots_.size();
            Val x; x.i = v.ival; x.d = v.dval;
            gslots_.push_back(x);
        }
        for (auto& [n, a] : prog_.globalArrays) {
            garrIndex_[n] = (int)garrays_.size();
            std::vector<Val> arr(a.len);
            for (long long i = 0; i < a.len; ++i) { arr[i].d = a.init[i]; arr[i].i = (long long)a.init[i]; }
            garrays_.push_back(std::move(arr));
            garrayFp_.push_back(a.ty == Ty::Double);
        }
        for (size_t i = 0; i < prog_.funcs.size(); ++i) funcIndex_[prog_.funcs[i].name] = (int)i;
        funcs_.resize(prog_.funcs.size());
        for (size_t fi = 0; fi < prog_.funcs.size(); ++fi) {
            const IRFunc& f = prog_.funcs[fi];
            PFunc& pf = funcs_[fi];
            pf.name = f.name;
            std::unordered_map<std::string, int> slot, arrSlot, labels;
            auto slotOf = [&](const std::string& n) {
                auto it = slot.find(n);
                if (it != slot.end()) return it->second;
                int s = pf.nSlots++;
                slot[n] = s;
                return s;
            };
            for (auto& p : f.params) pf.paramSlots.push_back(slotOf(p.name));
            for (auto& [n, a] : f.arrays) { arrSlot[n] = (int)pf.arrays.size(); pf.arrays.push_back({a.len, a.ty == Ty::Double}); }
            for (size_t i = 0; i < f.code.size(); ++i)
                if (f.code[i].op == Op::Label) labels[f.code[i].dst.name] = (int)i;
            auto res = [&](const Operand& o) {
                POp p;
                p.fp = o.ty == Ty::Double;
                switch (o.k) {
                    case OK::ConstI: p.kind = POp::Const; p.c.i = o.ival; p.c.d = (double)o.ival; break;
                    case OK::ConstD: p.kind = POp::Const; p.c.d = o.dval; p.c.i = (long long)o.dval; break;
                    case OK::Temp: p.kind = POp::Local; p.slot = slotOf(o.name); break;
                    case OK::Var:
                        if (o.global) { p.kind = POp::Global; p.slot = gslotIndex_.at(o.name); }
                        else { p.kind = POp::Local; p.slot = slotOf(o.name); }
                        break;
                    default: break;
                }
                return p;
            };
            for (auto& q : f.code) {
                PQuad pq;
                pq.op = q.op;
                pq.dst = res(q.dst);
                bool arrayOp = q.op == Op::Load || q.op == Op::Store;
                if (!arrayOp && q.op != Op::Call && q.op != Op::PrintS) pq.a = res(q.a);
                pq.b = res(q.b); pq.c = res(q.c);
                if (q.op == Op::Jmp || q.op == Op::JmpF) pq.target = labels.at(q.dst.name);
                if (q.op == Op::Call) { pq.target = funcIndex_.at(q.a.name); pq.nargs = q.b.ival; pq.b = {}; }
                if (q.op == Op::Load || q.op == Op::Store) {
                    if (q.a.global) { pq.arr = garrIndex_.at(q.a.name); pq.arrGlobal = true; }
                    else pq.arr = arrSlot.at(q.a.name);
                }
                if (q.op == Op::PrintS) pq.str = q.a.name;
                pf.code.push_back(std::move(pq));
            }
            res_.counts[f.name].assign(f.code.size(), 0);
        }
    }

    Frame makeFrame(int fi) {
        Frame fr;
        fr.fn = &funcs_[fi];
        fr.slots.assign(fr.fn->nSlots, Val{});
        for (auto& [len, fp] : fr.fn->arrays) fr.arrays.emplace_back(len);
        fr.counts = &res_.counts[fr.fn->name];
        return fr;
    }

    void fail(const std::string& m, const Frame& fr) {
        res_.ok = false;
        res_.error = "runtime error in '" + fr.fn->name + "': " + m;
    }

    void exec(int mainIdx) {
        std::vector<Frame> stack;
        stack.push_back(makeFrame(mainIdx));
        std::vector<Val> args;
        uint64_t steps = 0;

        while (!stack.empty()) {
            Frame& fr = stack.back();
            if (fr.pc >= fr.fn->code.size()) { fail("fell off the end of the function", fr); break; }
            const PQuad& q = fr.fn->code[fr.pc];
            (*fr.counts)[fr.pc]++;
            if (++steps > lim_.maxSteps) { fail("step limit exceeded (possible infinite loop)", fr); break; }
            fr.pc++;

            auto get = [&](const POp& o) -> Val {
                switch (o.kind) {
                    case POp::Const: return o.c;
                    case POp::Local: return fr.slots[o.slot];
                    case POp::Global: return gslots_[o.slot];
                    default: return Val{};
                }
            };
            auto set = [&](const POp& o, Val v) {
                if (o.kind == POp::Local) fr.slots[o.slot] = v;
                else if (o.kind == POp::Global) gslots_[o.slot] = v;
            };
            auto I = [](long long v) { Val x; x.i = v; x.d = (double)v; return x; };
            auto D = [](double v) { Val x; x.d = v; x.i = 0; return x; };
            bool fp = q.a.fp || q.dst.fp;

            switch (q.op) {
                case Op::Add: { Val a = get(q.a), b = get(q.b); set(q.dst, fp ? D(a.d + b.d) : I(wrap32(a.i + b.i))); break; }
                case Op::Sub: { Val a = get(q.a), b = get(q.b); set(q.dst, fp ? D(a.d - b.d) : I(wrap32(a.i - b.i))); break; }
                case Op::Mul: { Val a = get(q.a), b = get(q.b); set(q.dst, fp ? D(a.d * b.d) : I(wrap32(a.i * b.i))); break; }
                case Op::Div: {
                    Val a = get(q.a), b = get(q.b);
                    if (fp) { set(q.dst, D(a.d / b.d)); break; }
                    if (b.i == 0) { fail("integer division by zero", fr); stack.clear(); continue; }
                    set(q.dst, I(wrap32(a.i / b.i)));
                    break;
                }
                case Op::Mod: {
                    Val a = get(q.a), b = get(q.b);
                    if (b.i == 0) { fail("integer modulo by zero", fr); stack.clear(); continue; }
                    set(q.dst, I(wrap32(a.i % b.i)));
                    break;
                }
                case Op::Shl: { Val a = get(q.a), b = get(q.b); set(q.dst, I(wrap32((long long)((uint32_t)a.i << (b.i & 31))))); break; }
                case Op::Neg: { Val a = get(q.a); set(q.dst, fp ? D(-a.d) : I(wrap32(-a.i))); break; }
                case Op::Not: { Val a = get(q.a); set(q.dst, I(q.a.fp ? a.d == 0 : a.i == 0)); break; }
                case Op::Lt: case Op::Le: case Op::Gt: case Op::Ge: case Op::Eq: case Op::Ne: {
                    Val a = get(q.a), b = get(q.b);
                    bool f2 = q.a.fp || q.b.fp, r = false;
                    double x = f2 ? a.d : (double)a.i, y = f2 ? b.d : (double)b.i;
                    long long xi = a.i, yi = b.i;
                    switch (q.op) {
                        case Op::Lt: r = f2 ? x < y : xi < yi; break;
                        case Op::Le: r = f2 ? x <= y : xi <= yi; break;
                        case Op::Gt: r = f2 ? x > y : xi > yi; break;
                        case Op::Ge: r = f2 ? x >= y : xi >= yi; break;
                        case Op::Eq: r = f2 ? x == y : xi == yi; break;
                        default: r = f2 ? x != y : xi != yi; break;
                    }
                    set(q.dst, I(r));
                    break;
                }
                case Op::Copy: { Val a = get(q.a); set(q.dst, q.dst.fp ? D(q.a.fp ? a.d : (double)a.i) : I(a.i)); break; }
                case Op::I2D: { Val a = get(q.a); set(q.dst, D((double)a.i)); break; }
                case Op::D2I: {
                    Val a = get(q.a);
                    if (!std::isfinite(a.d) || a.d >= 2147483648.0 || a.d <= -2147483649.0) { fail("double to int conversion out of range", fr); stack.clear(); continue; }
                    set(q.dst, I((long long)a.d));
                    break;
                }
                case Op::Load: case Op::Store: {
                    std::vector<Val>& arr = q.arrGlobal ? garrays_[q.arr] : fr.arrays[q.arr];
                    long long idx = get(q.b).i;
                    if (idx < 0 || idx >= (long long)arr.size()) {
                        fail("array index " + std::to_string(idx) + " out of bounds (size " + std::to_string(arr.size()) + ")", fr);
                        stack.clear(); continue;
                    }
                    if (q.op == Op::Load) set(q.dst, arr[idx]);
                    else {
                        Val v = get(q.c);
                        bool afp = q.arrGlobal ? garrayFp_[q.arr] : fr.fn->arrays[q.arr].second;
                        if (afp && !q.c.fp) v = D((double)v.i);
                        arr[idx] = v;
                    }
                    break;
                }
                case Op::Label: case Op::Nop: break;
                case Op::Jmp: fr.pc = q.target; break;
                case Op::JmpF: { Val a = get(q.a); if (q.a.fp ? a.d == 0 : a.i == 0) fr.pc = q.target; break; }
                case Op::Param: args.push_back(get(q.a)); break;
                case Op::Call: {
                    if ((int)stack.size() >= lim_.maxDepth) { fail("call depth limit exceeded (runaway recursion?)", fr); stack.clear(); continue; }
                    Frame callee = makeFrame(q.target);
                    size_t n = (size_t)q.nargs;
                    for (size_t k = 0; k < n; ++k) callee.slots[callee.fn->paramSlots[k]] = args[args.size() - n + k];
                    args.resize(args.size() - n);
                    callee.retDst = q.dst;
                    stack.push_back(std::move(callee));   // `fr` is invalid after this
                    continue;
                }
                case Op::Ret: {
                    Val v = get(q.a);
                    POp dst = fr.retDst;
                    stack.pop_back();
                    if (stack.empty()) { res_.exitCode = (int)v.i; break; }
                    Frame& caller = stack.back();
                    if (dst.kind == POp::Local) caller.slots[dst.slot] = v;
                    else if (dst.kind == POp::Global) gslots_[dst.slot] = v;
                    continue;
                }
                case Op::Print: {
                    Val a = get(q.a);
                    if (q.a.fp) out_ << a.d; else out_ << a.i;
                    break;
                }
                case Op::PrintS: out_ << q.str; break;
                case Op::PrintNL: out_ << '\n'; break;
                default: break;
            }
        }
        res_.steps = steps;
    }
};

}  // namespace

RunResult interpret(const IRProgram& p, const RunLimits& lim) { return Machine(p, lim).run(); }

}  // namespace ww
