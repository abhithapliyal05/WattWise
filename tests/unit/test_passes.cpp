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

// ---------------------------------------------------------------- RLE ----

TEST_CASE("RLE: a repeated load of the same element is removed", "[passes][rle]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int a[4]; a[0] = 1; a[1] = 2; a[2] = 3; a[3] = 4; int i = 2;
                     int x = a[i]; int y = a[i]; cout << x * 10 + y << endl; return 0; })");
    std::string before = wwt::run(p);
    IRFunc& m = *p.find("main");
    int loads = wwt::countOp(m, Op::Load);
    auto st = loadElim(m, wwt::allHot());
    CHECK(st.changes == 1);
    CHECK(wwt::countOp(m, Op::Load) == loads - 1);
    CHECK(wwt::run(p) == before);
    CHECK(before == "33\n");
}

TEST_CASE("RLE: a load right after a store reuses the stored value", "[passes][rle]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int a[4]; int i = 1; int v = 41; a[i] = v; int y = a[i];
                     cout << y + 1 << endl; return 0; })");
    std::string before = wwt::run(p);
    IRFunc& m = *p.find("main");
    CHECK(wwt::countOp(m, Op::Load) == 1);
    loadElim(m, wwt::allHot());
    CHECK(wwt::countOp(m, Op::Load) == 0);
    CHECK(wwt::run(p) == before);
    CHECK(before == "42\n");
}

TEST_CASE("RLE: a store to the same array invalidates earlier loads, aliasing or not", "[passes][rle]") {
    for (int j : {1, 2}) {   // j == 1 aliases the loaded element, j == 2 does not
        auto p = wwt::compile("#include <iostream>\nusing namespace std;\n"
                              "int main() { int a[4]; a[0] = 5; a[1] = 6; a[2] = 7; a[3] = 8; int i = 1; int j = " +
                              std::to_string(j) + ";\n int x = a[i]; a[j] = 99; int y = a[i];\n"
                              "cout << x << \" \" << y << endl; return 0; }");
        std::string before = wwt::run(p);
        IRFunc& m = *p.find("main");
        int loads = wwt::countOp(m, Op::Load);
        loadElim(m, wwt::allHot());
        CHECK(wwt::countOp(m, Op::Load) == loads);   // the second load must survive
        CHECK(wwt::run(p) == before);
        CHECK(before == (j == 1 ? "6 99\n" : "6 6\n"));
    }
}

TEST_CASE("RLE: redefining the index invalidates the entry", "[passes][rle]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int a[4]; a[0] = 1; a[1] = 2; a[2] = 3; a[3] = 4; int i = 1;
                     int x = a[i]; i = i + 1; int y = a[i]; cout << x << " " << y << endl; return 0; })");
    std::string before = wwt::run(p);
    IRFunc& m = *p.find("main");
    int loads = wwt::countOp(m, Op::Load);
    loadElim(m, wwt::allHot());
    CHECK(wwt::countOp(m, Op::Load) == loads);
    CHECK(wwt::run(p) == before);
    CHECK(before == "2 3\n");
}

TEST_CASE("RLE: a call invalidates global arrays but not local arrays", "[passes][rle]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int g[2];
        void touch() { g[0] = 50; }
        int main() { int l[2]; g[0] = 1; l[0] = 2; int i = 0;
                     int gx = g[i]; int lx = l[i];
                     touch();
                     int gy = g[i]; int ly = l[i];
                     cout << gx << " " << gy << " " << lx << " " << ly << endl; return 0; })");
    std::string before = wwt::run(p);
    IRFunc& m = *p.find("main");
    int loads = wwt::countOp(m, Op::Load);
    loadElim(m, wwt::allHot());
    CHECK(wwt::countOp(m, Op::Load) == loads - 1);   // only the local-array reload goes
    CHECK(wwt::run(p) == before);
    CHECK(before == "1 50 2 2\n");
}

TEST_CASE("RLE: entries flow into a single-predecessor block but not across a join", "[passes][rle]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int a[4]; a[0] = 1; a[1] = 2; a[2] = 3; a[3] = 4; int i = 2; int c = 1;
                     int x = a[i]; int y = 0;
                     if (c > 0) { y = a[i]; }
                     int z = a[i];
                     cout << x << y << z << endl; return 0; })");
    std::string before = wwt::run(p);
    IRFunc& m = *p.find("main");
    int loads = wwt::countOp(m, Op::Load);
    loadElim(m, wwt::allHot());
    CHECK(wwt::countOp(m, Op::Load) == loads - 1);   // the load inside the if goes; the one after the join stays
    CHECK(wwt::run(p) == before);
    CHECK(before == "333\n");
}

TEST_CASE("CSE then RLE removes the reloads of a compare-and-swap", "[passes][rle][cse]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int v[4];
        int main() { v[0] = 4; v[1] = 3; v[2] = 2; v[3] = 1; int j = 1;
                     if (v[j] > v[j + 1]) { int tmp = v[j]; v[j] = v[j + 1]; v[j + 1] = tmp; }
                     cout << v[0] << v[1] << v[2] << v[3] << endl; return 0; })");
    std::string before = wwt::run(p);
    IRFunc& m = *p.find("main");
    int loads = wwt::countOp(m, Op::Load);
    localCSE(m, wwt::allHot());
    loadElim(m, wwt::allHot());
    CHECK(wwt::countOp(m, Op::Load) <= loads - 2);
    CHECK(wwt::run(p) == before);
    CHECK(before == "4231\n");
}

// ---------------------------------------------------------------- DCE ----

TEST_CASE("DCE removes an unused computation and the chain feeding it", "[passes][dce]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int a = 5; int b = 9; int unused = (a * b) + (a - b); cout << a << endl; return 0; })");
    std::string before = wwt::run(p);
    IRFunc& m = *p.find("main");
    auto st = deadCode(m, wwt::allHot());
    CHECK(st.changes >= 3);
    CHECK(wwt::countOp(m, Op::Mul) == 0);
    CHECK(wwt::run(p) == before);
}

TEST_CASE("DCE keeps a value that is used", "[passes][dce]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int a = 5; int b = 9; int x = a * b; cout << x << endl; return 0; })");
    IRFunc& m = *p.find("main");
    deadCode(m, wwt::allHot());
    CHECK(wwt::countOp(m, Op::Mul) == 1);
    CHECK(wwt::run(p) == "45\n");
}

TEST_CASE("DCE never removes global writes or array stores", "[passes][dce]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int g;
        int arr[2];
        void set() { g = 3; arr[1] = 8; }
        int main() { set(); cout << g << arr[1] << endl; return 0; })");
    for (auto& f : p.funcs) deadCode(f, wwt::allHot());
    CHECK(wwt::run(p) == "38\n");
}

TEST_CASE("DCE keeps a division that may trap", "[passes][dce]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int z = 0; int unused = 10 / z; cout << 1 << endl; return 0; })");
    CHECK_FALSE(interpret(p).ok);
    deadCode(*p.find("main"), wwt::allHot());
    CHECK_FALSE(interpret(p).ok);
}

TEST_CASE("DCE removes a dead load", "[passes][dce]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int a[2]; a[0] = 4; a[1] = 5; int i = 1; int unused = a[i];
                     cout << a[0] << endl; return 0; })");
    IRFunc& m = *p.find("main");
    int loads = wwt::countOp(m, Op::Load);
    deadCode(m, wwt::allHot());
    CHECK(wwt::countOp(m, Op::Load) == loads - 1);
    CHECK(wwt::run(p) == "4\n");
}

TEST_CASE("DCE deletes unreachable code even when nothing is hot", "[passes][dce]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int x = 3; return x; x = x + 100; cout << x << endl; return 0; })");
    IRFunc& m = *p.find("main");
    size_t before = m.code.size();
    HotSet none;
    auto st = deadCode(m, none);
    CHECK(st.changes > 0);
    CHECK(m.code.size() < before);
    auto r = interpret(p);
    CHECK(r.ok);
    CHECK(r.exitCode == 3);
}

TEST_CASE("DCE leaves cold dead assignments alone", "[passes][dce][hot]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int a = 5; int b = 9; int unused = a * b; cout << a << endl; return 0; })");
    IRFunc& m = *p.find("main");
    HotSet none;
    auto st = deadCode(m, none);
    CHECK(st.changes == 0);
    CHECK(wwt::countOp(m, Op::Mul) == 1);
}

TEST_CASE("optimize() accepts rle and dce and rejects unknown names", "[passes][pipeline]") {
    auto p = wwt::compile("int main() { return 0; }");
    CHECK_NOTHROW(optimize(p, wwt::allHot(), {"rle", "dce"}));
    CHECK_THROWS(optimize(p, wwt::allHot(), {"bogus"}));
}

TEST_CASE("full pipeline with RLE and DCE preserves output and lowers energy", "[passes][pipeline]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int a[64]; int b[64]; int s = 0;
          for (int i = 0; i < 64; i++) a[i] = i * 3 % 11;
          for (int i = 1; i < 64; i++) { int x = a[i] + a[i]; b[i] = x; int y = b[i] * 2;
                                         int d = a[i - 1] + y; s = s + x + y; }
          cout << s << endl; return 0; })");
    std::string before = wwt::run(p);
    double e0 = staticEnergy(p);
    auto stats = optimize(p, wwt::allHot(), {"all"});
    CHECK(stats.size() == 6);
    CHECK(wwt::run(p) == before);
    CHECK(staticEnergy(p) < e0 * 0.85);
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
