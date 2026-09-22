// M1 — parser and semantic analysis tests.
#include "catch_amalgamated.hpp"
#include "helpers.hpp"
using namespace ww;
using wwt::failPhase;

static const char* kMin = "int main() { return 0; }";

TEST_CASE("minimal and full-featured programs are accepted", "[parser]") {
    CHECK(failPhase(kMin).empty());
    CHECK(failPhase(R"(
        #include <iostream>
        using namespace std;
        const int N = 8;
        int g[N];
        double avg(int n) { double s = 0.0; for (int i = 0; i < n; i++) s += g[i]; return s / n; }
        int fact(int n) { if (n <= 1) return 1; return n * fact(n - 1); }
        int main() {
            int i = 0;
            while (i < N) { g[i] = i * i; i = i + 1; }
            bool ok = (i == N) && !(i < 0);
            cout << avg(N) << " " << fact(5) << " " << ok << endl;
            return 0;
        })").empty());
}

TEST_CASE("operator precedence follows C++", "[parser]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { cout << 2 + 3 * 4 << " " << (2 + 3) * 4 << " " << 10 - 4 - 3
                          << " " << (1 < 2 == 1) << " " << -3 % 2 << endl; return 0; })");
    CHECK(wwt::run(p) == "14 20 3 1 -1\n");
}

TEST_CASE("syntax errors", "[parser][invalid]") {
    CHECK(failPhase("int main() { int x = 1 return 0; }") == "syntax");      // missing ;
    CHECK(failPhase("int main() { if (1 { } return 0; }") == "syntax");     // missing )
    CHECK(failPhase("int main() { return 0;") == "syntax");                 // missing }
    CHECK(failPhase("int main() { x + ; return 0; }") == "syntax");
}

TEST_CASE("constructs outside the subset are rejected clearly", "[parser][invalid]") {
    for (const char* s : {"struct P { int x; }; int main() { return 0; }",
                          "class C {}; int main() { return 0; }",
                          "int main() { switch (1) { } return 0; }",
                          "template <typename T> T id(T x) { return x; } int main() { return 0; }"}) {
        INFO(s);
        CHECK(failPhase(s) == "syntax");
    }
}

TEST_CASE("panic-mode recovery reports several syntax errors", "[parser][invalid]") {
    try {
        wwt::compile("int main() { int x = ; int y = ; return 0; }");
        FAIL("expected CompileError");
    } catch (const CompileError& e) {
        CHECK(e.diags().size() >= 2);
    }
}

TEST_CASE("semantic errors", "[semantic][invalid]") {
    CHECK(failPhase("int main() { return y; }") == "semantic");                        // undeclared
    CHECK(failPhase("int main() { int x; int x; return 0; }") == "semantic");          // redeclared
    CHECK(failPhase("int main() { const int c = 1; c = 2; return 0; }") == "semantic");// const assign
    CHECK(failPhase("int f(int a) { return a; } int main() { return f(1, 2); }") == "semantic");
    CHECK(failPhase("void f() { } int main() { int x = f(); return 0; }") == "semantic");
    CHECK(failPhase("int main() { int a[4]; a[4] = 1; return 0; }") == "semantic");    // const OOB
    CHECK(failPhase("int main() { int x = 5 / 0; return 0; }") == "semantic");
    CHECK(failPhase("int main() { int x = 1; return x[0]; }") == "semantic");          // not array
    CHECK(failPhase("int main() { int a[3]; a = 1; return 0; }") == "semantic");
    CHECK(failPhase("int helper() { return 1; }") == "semantic");                      // no main
    CHECK(failPhase("int main() { return g(); } int g() { return 1; }") == "semantic");// use before decl
}

TEST_CASE("shadowing inner scopes gets unique IR names", "[semantic]") {
    auto p = wwt::compile(R"(#include <iostream>
        using namespace std;
        int main() { int x = 1; { int x = 2; cout << x; } cout << x << endl; return 0; })");
    CHECK(wwt::run(p) == "21\n");
}

TEST_CASE("narrowing double->int produces a warning, not an error", "[semantic]") {
    Program p = parse(lex("int main() { int x = 2.7; return x; }"));
    SemaResult s = analyze(p);
    CHECK_FALSE(s.warnings.empty());
    auto ir = generateIR(p, s);
    CHECK(interpret(ir).exitCode == 2);
}
