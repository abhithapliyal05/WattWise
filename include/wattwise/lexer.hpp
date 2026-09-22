// lexer.hpp — DFA-style scanner for the WattWise C++ subset (Module M1).
//
// Input : C++ source text (a restricted subset, see docs/subset.md)
// Output: vector<Token> terminated by an End token.
// Preprocessor lines (#include ...) are skipped; `using namespace std;` is
// passed through as tokens and ignored by the parser.
#pragma once
#include <string>
#include <vector>
#include "wattwise/common.hpp"

namespace ww {

enum class Tok {
    // literals / names
    Ident, IntLit, FloatLit, StrLit, CharLit,
    // keywords that belong to the subset
    KwInt, KwDouble, KwBool, KwVoid, KwConst, KwIf, KwElse, KwWhile, KwFor,
    KwReturn, KwTrue, KwFalse, KwUsing, KwNamespace,
    // punctuation / operators
    LParen, RParen, LBrace, RBrace, LBracket, RBracket, Semi, Comma,
    Plus, Minus, Star, Slash, Percent, Assign,
    PlusAssign, MinusAssign, StarAssign, SlashAssign, PercentAssign,
    PlusPlus, MinusMinus,
    Eq, Ne, Lt, Le, Gt, Ge, AndAnd, OrOr, Not, Shl, ColonColon,
    End
};

const char* tokName(Tok t);

struct Token {
    Tok kind;
    std::string text;   // lexeme (unescaped value for strings)
    long long ival = 0;
    double dval = 0.0;
    int line = 1, col = 1;
};

// Scans the whole input. Throws CompileError("lexical", ...) listing every
// lexical error found (the scanner skips the bad character and continues).
std::vector<Token> lex(const std::string& src);

}  // namespace ww
