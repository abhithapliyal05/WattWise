// lexer.cpp — hand-written maximal-munch scanner.
// Each branch of scanOne() corresponds to one DFA of the token classes:
// identifiers/keywords, numbers (int / float with exponent), strings,
// char literals, comments, and 1-2 character operators.
#include "wattwise/lexer.hpp"
#include <cctype>
#include <climits>
#include <unordered_map>

namespace ww {

const char* tokName(Tok t) {
    static const char* names[] = {
        "identifier", "int literal", "float literal", "string literal", "char literal",
        "'int'", "'double'", "'bool'", "'void'", "'const'", "'if'", "'else'", "'while'", "'for'",
        "'return'", "'true'", "'false'", "'using'", "'namespace'",
        "'('", "')'", "'{'", "'}'", "'['", "']'", "';'", "','",
        "'+'", "'-'", "'*'", "'/'", "'%'", "'='",
        "'+='", "'-='", "'*='", "'/='", "'%='", "'++'", "'--'",
        "'=='", "'!='", "'<'", "'<='", "'>'", "'>='", "'&&'", "'||'", "'!'", "'<<'", "'::'",
        "end of file"};
    return names[static_cast<int>(t)];
}

namespace {

const std::unordered_map<std::string, Tok> kKeywords = {
    {"int", Tok::KwInt}, {"double", Tok::KwDouble}, {"bool", Tok::KwBool},
    {"void", Tok::KwVoid}, {"const", Tok::KwConst}, {"if", Tok::KwIf},
    {"else", Tok::KwElse}, {"while", Tok::KwWhile}, {"for", Tok::KwFor},
    {"return", Tok::KwReturn}, {"true", Tok::KwTrue}, {"false", Tok::KwFalse},
    {"using", Tok::KwUsing}, {"namespace", Tok::KwNamespace}};

class Scanner {
public:
    explicit Scanner(const std::string& s) : src_(s) {}

    std::vector<Token> run() {
        while (true) {
            skipTrivia();
            if (pos_ >= src_.size()) break;
            scanOne();
        }
        out_.push_back(Token{Tok::End, "", 0, 0.0, line_, col_});
        if (!errs_.empty()) throw CompileError("lexical", errs_);
        return out_;
    }

private:
    const std::string& src_;
    size_t pos_ = 0;
    int line_ = 1, col_ = 1;
    std::vector<Token> out_;
    std::vector<Diag> errs_;

    char peek(size_t k = 0) const { return pos_ + k < src_.size() ? src_[pos_ + k] : '\0'; }
    char get() {
        char c = src_[pos_++];
        if (c == '\n') { line_++; col_ = 1; } else { col_++; }
        return c;
    }
    void error(int l, int c, const std::string& m) { errs_.push_back({l, c, m, false}); }

    // Whitespace, // and /* */ comments, and preprocessor lines.
    void skipTrivia() {
        while (pos_ < src_.size()) {
            char c = peek();
            if (std::isspace(static_cast<unsigned char>(c))) { get(); continue; }
            if (c == '/' && peek(1) == '/') { while (pos_ < src_.size() && peek() != '\n') get(); continue; }
            if (c == '/' && peek(1) == '*') {
                int l = line_, cc = col_;
                get(); get();
                bool closed = false;
                while (pos_ < src_.size()) {
                    if (peek() == '*' && peek(1) == '/') { get(); get(); closed = true; break; }
                    get();
                }
                if (!closed) error(l, cc, "unterminated block comment");
                continue;
            }
            // '#' at the start of a line (after optional spaces): preprocessor directive
            if (c == '#') { while (pos_ < src_.size() && peek() != '\n') get(); continue; }
            break;
        }
    }

    void push(Tok k, std::string text, int l, int c) {
        Token t; t.kind = k; t.text = std::move(text); t.line = l; t.col = c;
        out_.push_back(std::move(t));
    }

    void scanOne() {
        int l = line_, c = col_;
        char ch = peek();

        // identifiers & keywords: [A-Za-z_][A-Za-z0-9_]*
        if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
            std::string s;
            while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') s += get();
            auto it = kKeywords.find(s);
            push(it != kKeywords.end() ? it->second : Tok::Ident, s, l, c);
            return;
        }

        // numbers: digit+ ( '.' digit+ )? ( [eE] [+-]? digit+ )?
        if (std::isdigit(static_cast<unsigned char>(ch)) ||
            (ch == '.' && std::isdigit(static_cast<unsigned char>(peek(1))))) {
            std::string s;
            bool isFloat = false;
            while (std::isdigit(static_cast<unsigned char>(peek()))) s += get();
            if (peek() == '.') { isFloat = true; s += get(); while (std::isdigit(static_cast<unsigned char>(peek()))) s += get(); }
            if (peek() == 'e' || peek() == 'E') {
                size_t save = pos_; int sl = line_, sc = col_;
                std::string e(1, get());
                if (peek() == '+' || peek() == '-') e += get();
                if (std::isdigit(static_cast<unsigned char>(peek()))) {
                    while (std::isdigit(static_cast<unsigned char>(peek()))) e += get();
                    s += e; isFloat = true;
                } else { pos_ = save; line_ = sl; col_ = sc; }
            }
            if (std::isalpha(static_cast<unsigned char>(peek())) || peek() == '_') {
                std::string bad;
                while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') bad += get();
                error(l, c, "malformed number '" + s + bad + "' (suffixes are not in the subset)");
                return;
            }
            Token t; t.line = l; t.col = c; t.text = s;
            if (isFloat) { t.kind = Tok::FloatLit; t.dval = std::stod(s); }
            else {
                t.kind = Tok::IntLit;
                // reject literals that do not fit a 32-bit int
                if (s.size() > 10 || std::stoll(s) > INT_MAX) { error(l, c, "integer literal '" + s + "' out of range for int"); return; }
                t.ival = std::stoll(s);
            }
            out_.push_back(t);
            return;
        }

        // string literal with escapes
        if (ch == '"') {
            get();
            std::string v;
            while (true) {
                if (pos_ >= src_.size() || peek() == '\n') { error(l, c, "unterminated string literal"); return; }
                char x = get();
                if (x == '"') break;
                if (x == '\\') v += escape();
                else v += x;
            }
            push(Tok::StrLit, v, l, c);
            return;
        }

        // char literal (only usable inside cout)
        if (ch == '\'') {
            get();
            std::string v;
            if (peek() == '\\') { get(); v = escape(); }
            else if (peek() != '\'' && peek() != '\n' && pos_ < src_.size()) v = std::string(1, get());
            if (peek() != '\'' || v.empty()) { error(l, c, "malformed char literal"); while (pos_ < src_.size() && peek() != '\n' && peek() != ';') get(); return; }
            get();
            push(Tok::CharLit, v, l, c);
            return;
        }

        // operators: maximal munch over two-character forms first
        get();
        char n = peek();
        auto two = [&](char second, Tok k2, Tok k1) {
            if (n == second) { get(); push(k2, std::string{ch, second}, l, c); }
            else push(k1, std::string(1, ch), l, c);
        };
        switch (ch) {
            case '(': push(Tok::LParen, "(", l, c); break;
            case ')': push(Tok::RParen, ")", l, c); break;
            case '{': push(Tok::LBrace, "{", l, c); break;
            case '}': push(Tok::RBrace, "}", l, c); break;
            case '[': push(Tok::LBracket, "[", l, c); break;
            case ']': push(Tok::RBracket, "]", l, c); break;
            case ';': push(Tok::Semi, ";", l, c); break;
            case ',': push(Tok::Comma, ",", l, c); break;
            case '+': if (n == '+') { get(); push(Tok::PlusPlus, "++", l, c); } else two('=', Tok::PlusAssign, Tok::Plus); break;
            case '-': if (n == '-') { get(); push(Tok::MinusMinus, "--", l, c); } else two('=', Tok::MinusAssign, Tok::Minus); break;
            case '*': two('=', Tok::StarAssign, Tok::Star); break;
            case '/': two('=', Tok::SlashAssign, Tok::Slash); break;
            case '%': two('=', Tok::PercentAssign, Tok::Percent); break;
            case '=': two('=', Tok::Eq, Tok::Assign); break;
            case '!': two('=', Tok::Ne, Tok::Not); break;
            case '<': if (n == '<') { get(); push(Tok::Shl, "<<", l, c); } else two('=', Tok::Le, Tok::Lt); break;
            case '>':
                if (n == '>') { get(); error(l, c, "operator '>>' is not in the WattWise subset (no cin, no right shift)"); }
                else two('=', Tok::Ge, Tok::Gt);
                break;
            case '&': if (n == '&') { get(); push(Tok::AndAnd, "&&", l, c); } else error(l, c, "'&' (address-of / bitwise and) is not in the WattWise subset"); break;
            case '|': if (n == '|') { get(); push(Tok::OrOr, "||", l, c); } else error(l, c, "'|' (bitwise or) is not in the WattWise subset"); break;
            case ':': if (n == ':') { get(); push(Tok::ColonColon, "::", l, c); } else error(l, c, "unexpected ':' (labels, ?: and bit-fields are not in the WattWise subset)"); break;
            case '?': error(l, c, "conditional operator '?:' is not in the WattWise subset; use if/else"); break;
            default:
                error(l, c, std::string("unexpected character '") + ch + "'");
        }
    }

    std::string escape() {
        char e = pos_ < src_.size() ? get() : '\0';
        switch (e) {
            case 'n': return "\n";
            case 't': return "\t";
            case '\\': return "\\";
            case '"': return "\"";
            case '\'': return "'";
            case '0': return std::string(1, '\0');
            default: error(line_, col_, std::string("unknown escape sequence '\\") + e + "'"); return "";
        }
    }
};

}  // namespace

std::vector<Token> lex(const std::string& src) { return Scanner(src).run(); }

}  // namespace ww
