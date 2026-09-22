# Three-address IR

Quadruples `dst = a op b`, one per source-level operation. Names: `x` (local
variable; shadowed locals become `x.1`, `x.2`), `@g` (global), `tN` (temporary),
`LN` (label). Every quad carries its source line for per-line attribution.

| Form | Ops |
|---|---|
| `dst = a op b` | `Add Sub Mul Div Mod Shl Lt Le Gt Ge Eq Ne` |
| `dst = op a` | `Neg Not Copy I2D D2I` |
| `dst = a[b]` / `a[b] = c` | `Load` / `Store` |
| `L:` / `goto L` / `iffalse a goto L` | `Label` / `Jmp` / `JmpF` |
| `param a` / `dst = call f, n` / `return a` | `Param` / `Call` / `Ret` |
| `print a` / `print "s"` / `print endl` | `Print` / `PrintS` / `PrintNL` |

`&&` and `||` are lowered to short-circuit control flow. Assignment
`x = a + b` is emitted as a single quad (no copy through a temporary).

Example — `benchmarks/hoist.cpp` inner loop before optimisation:

```
L1:
    t1 = i < n
    iffalse t1 goto L2
    t2 = n * 2
    t3 = n / 2
    t4 = t2 + t3
    @a[i] = t4
    i = i + 1
    goto L1
```

After `--opt`: `t2 = n << 1`, and `t2`, `t3`, `t4` hoisted into a new preheader `L3`.

`wattwise file.cpp --ir` prints the IR; `--ir-opt` prints it after optimisation;
`--cfg` prints basic blocks and loops; `--dot DIR` writes Graphviz CFGs coloured
by energy.
