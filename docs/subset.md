# The WattWise C++ subset

WattWise compiles a restricted subset of C++17. **Every program WattWise accepts is
also a valid C++ program**, so `g++` is the ground-truth oracle for differential
testing (`scripts/difftest.sh`). This replaces the custom *EnerC* language proposed
at Review 1.

## Grammar (EBNF)

```ebnf
program      = { top_decl } ;
top_decl     = "using" "namespace" "std" ";"
             | var_decl ";"
             | func_def ;
func_def     = type IDENT "(" [ params | "void" ] ")" block ;
params       = type IDENT { "," type IDENT } ;              (* scalars only *)
type         = "int" | "double" | "bool" | "void" ;
var_decl     = [ "const" ] type declarator { "," declarator } ;
declarator   = IDENT [ "[" expr "]" ] [ "=" ( expr | "{" [ expr { "," expr } ] "}" ) ] ;

block        = "{" { var_decl ";" | stmt } "}" ;
stmt         = block | ";"
             | "if" "(" expr ")" stmt [ "else" stmt ]
             | "while" "(" expr ")" stmt
             | "for" "(" [ for_init ] ";" [ expr ] ";" [ simple ] ")" stmt
             | "return" [ expr ] ";"
             | print ";"
             | simple ";" ;
for_init     = var_decl | simple ;                           (* one declarator *)
simple       = lvalue assign_op expr
             | lvalue ( "++" | "--" ) | ( "++" | "--" ) lvalue
             | call ;
assign_op    = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
print        = [ "std" "::" ] "cout" "<<" item { "<<" item } ;
item         = STRING | CHAR | [ "std" "::" ] "endl" | additive ;

expr         = or_expr ;
or_expr      = and_expr { "||" and_expr } ;
and_expr     = equality { "&&" equality } ;
equality     = relational { ( "==" | "!=" ) relational } ;
relational   = additive { ( "<" | "<=" | ">" | ">=" ) additive } ;
additive     = multiplicative { ( "+" | "-" ) multiplicative } ;
multiplicative = unary { ( "*" | "/" | "%" ) unary } ;
unary        = ( "-" | "+" | "!" ) unary | postfix ;
postfix      = INT | FLOAT | "true" | "false" | "(" expr ")"
             | IDENT [ "(" [ expr { "," expr } ] ")" | "[" expr "]" ] ;
lvalue       = IDENT [ "[" expr "]" ] ;
```

`#include` lines are skipped by the lexer. Array sizes must be integer constant
expressions (literals and `const int` globals/locals, folded at compile time).

## Deliberately excluded (rejected with an "outside the WattWise subset" error)

| Group | Excluded | Why |
|---|---|---|
| Types | `char`, `float`, `long`, `unsigned`, `string`, `vector`, `auto`, pointers, references | keeps the cost model and interpreter small; `int` is 32-bit wrap-around |
| Aggregates | `struct`, `class`, `union`, `enum`, `template` | no object model needed for energy analysis |
| Control flow | `switch`, `do`, `break`, `continue`, `goto`, `?:` | loops stay single-exit natural loops, which simplifies LICM preheaders |
| Arrays | array parameters, multi-dimensional arrays | no aliasing through parameters — the Review 1 "heavy pointer/alias code" limit |
| Operators | `>>`, `&`, `|`, `^`, `~`, `++`/`--` inside expressions | no side effects inside expressions, so every quad has one effect |
| I/O | `cin`, `printf` | output only via `cout <<`; I/O energy is out of scope |

Twelve of the 33 programs in `tests/invalid/` are valid C++ that `g++` accepts but
WattWise rejects on purpose; `scripts/invalidtest.sh` labels them "subset rule".
