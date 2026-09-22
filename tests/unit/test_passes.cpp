// M3 — optimisation passes: each must reduce cost and never change output.
#include "catch_amalgamated.hpp"
#include "helpers.hpp"
using namespace ww;

static double staticEnergy(const IRProgram& p) {
    return analyzeStatic(p, buildAllCFGs(p), CostModel::defaults()).total;
}

TEST_CASE("strength reduction: x*2 -> x<<1, algebraic identities", "[passes][sr]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int n = 21; int a = n * 2; int b = n * 1; int c = n + 0; int d = n * 8;
                     cout << a << " " << b << " " << c << " " << d << endl; return 0; })");
    std::string before = wwt::run(p);
    IRFunc& m = *p.find("main");
    int mulsBefore = wwt::countOp(m, Op::Mul);
    auto st = strengthReduce(m, wwt::allHot());
    CHECK(st.changes >= 3);
    CHECK(wwt::countOp(m, Op::Mul) < mulsBefore);
    CHECK(wwt::countOp(m, Op::Shl) >= 2);
    CHECK(wwt::run(p) == before);
}

TEST_CASE("strength reduction preserves negative-number semantics of multiply", "[passes][sr]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int n = -7; cout << n * 4 << " " << n / 2 << " " << n % 2 << endl; return 0; })");
    std::string before = wwt::run(p);
    strengthReduce(*p.find("main"), wwt::allHot());
    CHECK(wwt::run(p) == before);
    CHECK(before == "-28 -3 -1\n");
}

TEST_CASE("local CSE removes a repeated expression", "[passes][cse]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int a = 5; int b = 9; int x = (a * b) + 1; int y = (a * b) + 2;
                     cout << x << " " << y << endl; return 0; })");
    std::string before = wwt::run(p);
    IRFunc& m = *p.find("main");
    auto st = localCSE(m, wwt::allHot());
    CHECK(st.changes >= 1);
    CHECK(wwt::countOp(m, Op::Mul) == 1);
    CHECK(wwt::run(p) == before);
}

TEST_CASE("CSE respects kills: redefined operand blocks reuse", "[passes][cse]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int a = 5; int b = 9; int x = a * b; a = 6; int y = a * b;
                     cout << x << " " << y << endl; return 0; })");
    std::string before = wwt::run(p);
    localCSE(*p.find("main"), wwt::allHot());
    CHECK(wwt::run(p) == before);
    CHECK(before == "45 54\n");
}

TEST_CASE("LICM hoists an invariant into a preheader", "[passes][licm]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int a[100]; int n = 100; int i = 0;
          while (i < n) { a[i] = (n * 2) + (n / 2); i = i + 1; }
          cout << a[0] + a[99] << endl; return 0; })");
    std::string before = wwt::run(p);
    double e0 = staticEnergy(p);
    auto st = licm(*p.find("main"), wwt::allHot());
    CHECK(st.changes >= 1);
    CHECK(wwt::run(p) == before);
    CHECK(staticEnergy(p) < e0 * 0.75);
}

TEST_CASE("LICM does not hoist a possibly-trapping division out of a zero-trip loop", "[passes][licm]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int z = 0; int s = 0; int i = 0;
          while (i < z) { s = s + 10 / z; i = i + 1; }
          cout << s << endl; return 0; })");
    licm(*p.find("main"), wwt::allHot());
    auto r = interpret(p);
    CHECK(r.ok);
    CHECK(r.output == "0\n");
}

TEST_CASE("LICM does not hoist a variant expression", "[passes][licm]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int s = 0; for (int i = 0; i < 10; i++) { int k = i * 3; s = s + k; }
                     cout << s << endl; return 0; })");
    std::string before = wwt::run(p);
    licm(*p.find("main"), wwt::allHot());
    CHECK(wwt::run(p) == before);
}

TEST_CASE("cold code is not transformed under profile direction", "[passes][hot]") {
    auto p = wwt::compile("int main() { int n = 3; int a = n * 2; return a; }");
    HotSet none;   // nothing is hot
    auto st = strengthReduce(*p.find("main"), none);
    CHECK(st.changes == 0);
}

TEST_CASE("full pipeline preserves output and never increases energy", "[passes][pipeline]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        const int N = 12;
        int main() { int A[144]; int s = 0;
          for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) A[i * N + j] = i * 2 + j;
          for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) s = s + A[i * N + j] * (N / 3);
          cout << s << endl; return 0; })");
    std::string before = wwt::run(p);
    double e0 = staticEnergy(p);
    auto stats = optimize(p, wwt::allHot(), {"sr", "cse", "licm", "cse"});
    CHECK(stats.size() == 4);
    CHECK(wwt::run(p) == before);
    CHECK(staticEnergy(p) <= e0);
}
