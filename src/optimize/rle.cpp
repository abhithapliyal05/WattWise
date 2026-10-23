// rle.cpp — redundant-load elimination over extended basic blocks.
//
// The pass tracks which array elements currently have a known copy in a
// scalar name or constant, as a map  (array, index operand) -> value operand:
//   x = a[i]   afterwards a[i] is available in x
//   a[i] = v   afterwards a[i] is available in v   (store-to-load forwarding)
// A later  y = a[i]  with the same array and the same index operand is then
// redundant: it becomes  y = v  (a cheap move) or, when both y and v are
// single-assignment temporaries / constants, disappears and y's uses are
// renamed to v.
//
// Invalidation (the subset has no pointers or array parameters, so distinct
// arrays never alias and a callee can only reach global arrays):
//   - a store to array A drops every entry of A, except entries whose constant
//     index provably differs from the stored constant index;
//   - redefining a scalar drops entries that use it as index or value;
//   - a call drops all global-array entries and entries using global scalars.
// Scope is the extended basic block: a block entered only from one earlier
// block inherits that block's entries; joins and loop headers start empty.
//
// A removed load can no longer trap, but an identical load on the same path
// executed first with the same index, so no trap is hidden.
#include <map>
#include <tuple>
#include "wattwise/cfg.hpp"
#include "wattwise/optimize/passes.hpp"

namespace ww {
namespace {

using Key = std::tuple<std::string, bool, Operand>;   // (array name, global?, index)
using Avail = std::map<Key, Operand>;                 // -> operand holding the element

bool sameArray(const Key& k, const Operand& arr) { return std::get<0>(k) == arr.name && std::get<1>(k) == arr.global; }

}  // namespace

PassStats loadElim(IRFunc& f, const HotSet& hot) {
    PassStats st; st.pass = "redundant-load-elim";

    std::map<std::string, int> defs;   // temp -> number of definitions
    for (auto& q : f.code) if (const Operand* d = defOf(q); d && d->k == OK::Temp) defs[d->name]++;
    auto singleTemp = [&](const Operand& o) { return o.k == OK::Temp && defs[o.name] == 1; };

    CFG g = buildCFG(f);
    std::map<std::string, Operand> rename;   // eliminated temp -> surviving operand
    std::vector<Avail> out(g.blocks.size());

    for (auto& b : g.blocks) {
        Avail avail;
        if (b.pred.size() == 1 && b.pred[0] < b.id && g.blocks[b.pred[0]].reachable) avail = out[b.pred[0]];

        auto killScalar = [&](const Operand& d) {
            for (auto it = avail.begin(); it != avail.end();)
                it = (std::get<2>(it->first) == d || it->second == d) ? avail.erase(it) : std::next(it);
        };

        for (int i = b.begin; i < b.end; ++i) {
            Quad& q = f.code[i];
            for (Operand* u : useSlots(q)) {
                auto it = rename.find(u->name);
                if (u->k == OK::Temp && it != rename.end()) *u = it->second;
            }

            switch (q.op) {
                case Op::Load: {
                    Key k{q.a.name, q.a.global, q.b};
                    auto it = avail.find(k);
                    if (it != avail.end() && it->second.ty == q.dst.ty && hot.isHotLine(f.name, q.line)) {
                        Operand v = it->second;
                        st.changes++;
                        st.log.push_back("line " + std::to_string(q.line) + ": " + q.str() + "  =>  reuse " + v.str());
                        if (singleTemp(q.dst) && (v.isConst() || singleTemp(v))) {
                            rename[q.dst.name] = v;
                            q.op = Op::Nop;
                        } else {
                            Operand dst = q.dst;
                            q.op = Op::Copy; q.a = v; q.b = {}; q.c = {};
                            killScalar(dst);
                        }
                        break;
                    }
                    Operand dst = q.dst;
                    killScalar(dst);
                    if (!(dst == q.b)) avail[k] = dst;
                    break;
                }
                case Op::Store: {
                    for (auto it = avail.begin(); it != avail.end();) {
                        bool differs = std::get<2>(it->first).k == OK::ConstI && q.b.k == OK::ConstI &&
                                       std::get<2>(it->first).ival != q.b.ival;
                        it = (sameArray(it->first, q.a) && !differs) ? avail.erase(it) : std::next(it);
                    }
                    if (q.c.k != OK::None) avail[Key{q.a.name, q.a.global, q.b}] = q.c;
                    break;
                }
                case Op::Call: {
                    for (auto it = avail.begin(); it != avail.end();) {
                        const Operand& idx = std::get<2>(it->first);
                        bool dies = std::get<1>(it->first) || idx.global || it->second.global;
                        it = dies ? avail.erase(it) : std::next(it);
                    }
                    if (const Operand* d = defOf(q)) killScalar(*d);
                    break;
                }
                default:
                    if (const Operand* d = defOf(q)) killScalar(*d);
                    break;
            }
        }
        out[b.id] = std::move(avail);
    }

    for (auto& q : f.code)
        for (Operand* u : useSlots(q))
            if (u->k == OK::Temp) { auto it = rename.find(u->name); while (it != rename.end()) { *u = it->second; it = u->k == OK::Temp ? rename.find(u->name) : rename.end(); } }
    compact(f);
    return st;
}

}  // namespace ww
