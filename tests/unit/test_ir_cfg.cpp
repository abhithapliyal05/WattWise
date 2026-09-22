// M2 — IR generation, CFG, dominators, loops and liveness.
#include "catch_amalgamated.hpp"
#include "helpers.hpp"
using namespace ww;

static const char* kLoop = R"(#include <iostream>
using namespace std;
int main() {
    int s = 0;
    for (int i = 0; i < 10; i++) {
        int j = 0;
        while (j < i) { s = s + j; j = j + 1; }
    }
    cout << s << endl;
    return 0;
})";

TEST_CASE("assignment of a binary expression is a single quad", "[ir]") {
    auto p = wwt::compile("int main() { int a = 1; int b = 2; int x = 0; x = a + b; return x; }");
    const IRFunc* m = p.find("main");
    REQUIRE(m);
    int adds = 0;
    for (auto& q : m->code)
        if (q.op == Op::Add) { ++adds; CHECK(q.dst.k == OK::Var); }
    CHECK(adds == 1);
}

TEST_CASE("mixed arithmetic inserts I2D conversion", "[ir]") {
    auto p = wwt::compile("int main() { int a = 3; double d = a * 1.5; return 0; }");
    CHECK(wwt::countOp(*p.find("main"), Op::I2D) >= 1);
}

TEST_CASE("short-circuit && does not evaluate its RHS", "[ir]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int z = 0;
        bool touch() { z = z + 1; return true; }
        int main() { bool r = false && touch(); bool q = true || touch();
                     cout << z << r << q << endl; return 0; })");
    CHECK(wwt::run(p) == "001\n");
}

TEST_CASE("CFG blocks partition the function", "[cfg]") {
    auto p = wwt::compile(kLoop);
    const IRFunc& f = *p.find("main");
    CFG g = buildCFG(f);
    REQUIRE(!g.blocks.empty());
    int covered = 0;
    for (auto& b : g.blocks) { CHECK(b.begin < b.end); covered += b.end - b.begin; }
    CHECK(covered == (int)f.code.size());
    CHECK(g.blocks[0].reachable);
    // succ / pred are symmetric
    for (auto& b : g.blocks)
        for (int s : b.succ) {
            auto& pr = g.blocks[s].pred;
            CHECK(std::find(pr.begin(), pr.end(), b.id) != pr.end());
        }
}

TEST_CASE("entry dominates every reachable block", "[cfg]") {
    auto p = wwt::compile(kLoop);
    CFG g = buildCFG(*p.find("main"));
    for (auto& b : g.blocks) if (b.reachable) CHECK(g.dominates(0, b.id));
}

TEST_CASE("nested natural loops are found, inner first", "[cfg][loops]") {
    auto p = wwt::compile(kLoop);
    CFG g = buildCFG(*p.find("main"));
    REQUIRE(g.loops.size() == 2);
    CHECK(g.loops[0].depth == 2);
    CHECK(g.loops[1].depth == 1);
    CHECK(g.loops[0].parent == 1);
    for (int b : g.loops[0].blocks) CHECK(g.loops[1].blocks.count(b) == 1);
    for (auto& L : g.loops) {
        CHECK(!L.latches.empty());
        CHECK(!L.exitTargets.empty());
        for (int l : L.latches) CHECK(g.dominates(L.header, l));
    }
}

TEST_CASE("liveness: loop-carried variable is live into the header", "[cfg][liveness]") {
    auto p = wwt::compile(kLoop);
    const IRFunc& f = *p.find("main");
    CFG g = buildCFG(f);
    Liveness lv = computeLiveness(f, g);
    bool sLive = false;
    for (auto& o : lv.in[g.loops[1].header]) if (o.name == "s") sLive = true;
    CHECK(sLive);
    // nothing is live into the entry block
    CHECK(lv.in[0].empty());
}

TEST_CASE("IR interpreter matches expected semantics", "[ir][interp]") {
    auto p = wwt::compile(kLoop);
    CHECK(wwt::run(p) == "120\n");
}

TEST_CASE("runtime errors are caught, not crashes", "[interp]") {
    auto p = wwt::compile("int main() { int a[3]; int i = 5; a[i] = 1; return 0; }");
    auto r = interpret(p);
    CHECK_FALSE(r.ok);
    auto q = wwt::compile("int main() { int z = 0; int x = 7 / z; return x; }");
    CHECK_FALSE(interpret(q).ok);
}
