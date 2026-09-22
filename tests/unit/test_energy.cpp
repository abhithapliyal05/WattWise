// M2 / M3 / M4 — cost model, frequency estimation, energy analysis.
#include <fstream>
#include "catch_amalgamated.hpp"
#include "helpers.hpp"
using namespace ww;

TEST_CASE("default cost table encodes memory >> ALU ordering", "[energy][model]") {
    auto m = CostModel::defaults();
    auto& t = m.table();
    CHECK(t.at("mem.load") > t.at("int.alu") * 5);
    CHECK(t.at("mem.store") > t.at("mem.load"));
    CHECK(t.at("int.div") > t.at("int.mul"));
    CHECK(t.at("int.mul") > t.at("int.shift"));
    CHECK(t.at("fp.div") > t.at("fp.alu"));
}

TEST_CASE("quad categories", "[energy][model]") {
    Quad q; q.op = Op::Mul; q.dst = Operand::temp("t1", Ty::Int);
    q.a = Operand::var("a", Ty::Int, false); q.b = Operand::ci(3);
    CHECK(CostModel::category(q) == "int.mul");
    q.op = Op::Shl;  CHECK(CostModel::category(q) == "int.shift");
    q.op = Op::Load; CHECK(CostModel::category(q) == "mem.load");
    Quad d; d.op = Op::Div; d.a = Operand::cd(1.0); d.b = Operand::cd(2.0); d.dst = Operand::temp("t2", Ty::Double);
    CHECK(CostModel::category(d) == "fp.div");
}

TEST_CASE("cost file overrides entries", "[energy][model]") {
    std::ofstream("wwt_costs.txt") << "# test\nmem.load 99\n";
    auto m = CostModel::defaults();
    std::string err;
    REQUIRE(m.loadFile("wwt_costs.txt", err));
    CHECK(m.table().at("mem.load") == Catch::Approx(99));
    CHECK(m.table().at("int.alu") == Catch::Approx(1));
}

TEST_CASE("exact trip count from constant bounds", "[energy][freq]") {
    auto p = wwt::compile("int main() { int s = 0; for (int i = 0; i < 25; i++) s += i; return s; }");
    auto cfgs = buildAllCFGs(p);
    auto f = estimateStatic(p, cfgs);
    REQUIRE(f["main"].loops.size() == 1);
    CHECK(f["main"].loops[0].exact);
    CHECK(f["main"].loops[0].trip == Catch::Approx(25));
}

TEST_CASE("unknown bound falls back to the default trip count", "[energy][freq]") {
    auto p = wwt::compile("int f(int n) { int s = 0; while (s < n) s = s * 2 + 1; return s; } int main() { return f(9); }");
    auto f = estimateStatic(p, buildAllCFGs(p));
    REQUIRE(f["f"].loops.size() == 1);
    CHECK_FALSE(f["f"].loops[0].exact);
    CHECK(f["f"].loops[0].trip == Catch::Approx(kDefaultTrip));
}

TEST_CASE("static energy equals Σ cost × freq and sorts hotspots", "[energy][analysis]") {
    auto p = wwt::compile(R"(int main() { int a[64]; int n = 64;
        for (int i = 0; i < n; i++) a[i] = (n * 2) + (n / 2); return 0; })");
    auto cfgs = buildAllCFGs(p);
    auto r = analyzeStatic(p, cfgs, CostModel::defaults());
    double sum = 0;
    for (auto& b : r.blocks) { CHECK(b.energy == Catch::Approx(b.freq * b.costPerExec)); sum += b.energy; }
    CHECK(r.total == Catch::Approx(sum));
    for (size_t i = 1; i < r.blocks.size(); ++i) CHECK(r.blocks[i - 1].energy >= r.blocks[i].energy);
    REQUIRE(!r.loops.empty());
    CHECK(r.loops[0].energy / r.total > 0.9);   // the loop dominates
}

TEST_CASE("dynamic profile agrees with static on a counted loop", "[energy][analysis]") {
    auto p = wwt::compile("int main() { int s = 0; for (int i = 0; i < 100; i++) s = s + i * 3; return 0; }");
    auto cfgs = buildAllCFGs(p);
    auto m = CostModel::defaults();
    auto st = analyzeStatic(p, cfgs, m);
    auto dy = analyzeDynamic(p, cfgs, m, interpret(p));
    auto ag = compareRankings(st, dy, 3);
    CHECK(ag.topMatches);
    CHECK(st.total == Catch::Approx(dy.total).epsilon(0.05));
}

TEST_CASE("selectHot covers the requested fraction", "[energy][analysis]") {
    auto p = wwt::compile("int main() { int s = 0; for (int i = 0; i < 50; i++) s = s + i; return s; }");
    auto r = analyzeStatic(p, buildAllCFGs(p), CostModel::defaults());
    HotSet h = selectHot(r, 0.9);
    CHECK_FALSE(h.loops["main"].empty());
}
