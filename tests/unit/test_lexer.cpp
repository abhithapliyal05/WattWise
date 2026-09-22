// M1 — lexical analysis tests.
#include "catch_amalgamated.hpp"
#include "helpers.hpp"
using namespace ww;

TEST_CASE("keywords, identifiers and punctuation", "[lexer]") {
    auto t = lex("int x = 42; double y;");
    REQUIRE(t.size() == 9);
    CHECK(t[0].kind == Tok::KwInt);
    CHECK(t[1].kind == Tok::Ident);
    CHECK(t[1].text == "x");
    CHECK(t[2].kind == Tok::Assign);
    CHECK(t[3].kind == Tok::IntLit);
    CHECK(t[3].ival == 42);
    CHECK(t[4].kind == Tok::Semi);
    CHECK(t[5].kind == Tok::KwDouble);
    CHECK(t.back().kind == Tok::End);
}

TEST_CASE("maximal munch on multi-character operators", "[lexer]") {
    auto t = lex("a<=b == c != d && e || !f ++ -- += <<");
    std::vector<Tok> want = {Tok::Ident, Tok::Le, Tok::Ident, Tok::Eq, Tok::Ident, Tok::Ne,
                             Tok::Ident, Tok::AndAnd, Tok::Ident, Tok::OrOr, Tok::Not,
                             Tok::Ident, Tok::PlusPlus, Tok::MinusMinus, Tok::PlusAssign,
                             Tok::Shl, Tok::End};
    REQUIRE(t.size() == want.size());
    for (size_t i = 0; i < want.size(); ++i) CHECK(t[i].kind == want[i]);
}

TEST_CASE("float literals with fraction and exponent", "[lexer]") {
    auto t = lex("3.5 1e3 2.5e-2 .5");
    CHECK(t[0].kind == Tok::FloatLit);
    CHECK(t[0].dval == Catch::Approx(3.5));
    CHECK(t[1].dval == Catch::Approx(1000.0));
    CHECK(t[2].dval == Catch::Approx(0.025));
    CHECK(t[3].dval == Catch::Approx(0.5));
}

TEST_CASE("string escapes are unescaped", "[lexer]") {
    auto t = lex(R"("a\tb\n")");
    REQUIRE(t[0].kind == Tok::StrLit);
    CHECK(t[0].text == "a\tb\n");
}

TEST_CASE("comments and preprocessor lines are skipped", "[lexer]") {
    auto t = lex("#include <iostream>\n// line\n/* block\n */ int");
    REQUIRE(t.size() == 2);
    CHECK(t[0].kind == Tok::KwInt);
    CHECK(t[0].line == 4);
}

TEST_CASE("line and column tracking", "[lexer]") {
    auto t = lex("int\n  x;");
    CHECK(t[1].line == 2);
    CHECK(t[1].col == 3);
}

TEST_CASE("lexical errors are reported", "[lexer][invalid]") {
    CHECK_THROWS_AS(lex("int x = 5 @ 3;"), CompileError);
    CHECK_THROWS_AS(lex("\"unterminated"), CompileError);
    CHECK_THROWS_AS(lex("/* never closed"), CompileError);
    CHECK_THROWS_AS(lex("int x = 99999999999;"), CompileError);   // int range
    CHECK_THROWS_AS(lex("x = a >> 1;"), CompileError);             // outside subset
    CHECK_THROWS_AS(lex("x = a & b;"), CompileError);
}

TEST_CASE("multiple lexical errors are collected", "[lexer][invalid]") {
    try {
        lex("int a = 1 $ 2;\nint b = 3 @ 4;");
        FAIL("expected CompileError");
    } catch (const CompileError& e) {
        CHECK(e.phase() == "lexical");
        CHECK(e.diags().size() >= 2);
    }
}
