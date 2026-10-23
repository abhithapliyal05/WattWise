# WattWise — Compiler-Assisted Energy Profiling and Optimization

BCSE307 Compiler Design · Team A35 · Review 3 build (v0.3.0)

WattWise compiles a **small subset of C++** to three-address IR, builds a CFG with
dominators and natural loops, estimates energy per basic block / loop / function
from a documented cost model, ranks hotspots, applies energy-directed optimisation
passes to the hot code, and reports the before/after difference — verifying that
program output is unchanged.

Because the accepted language is real C++, every test program is also checked
against `g++`.

## Build

Requirements: a C++17 compiler (g++ ≥ 11 or clang ≥ 14) and CMake ≥ 3.16.

```sh
cmake -S . -B build -DWATTWISE_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure     # unit + differential + invalid
```

## Use

```sh
build/wattwise benchmarks/hoist.cpp                 # static energy report
build/wattwise benchmarks/hoist.cpp --both          # static vs dynamic + hotspot agreement
build/wattwise benchmarks/hoist.cpp --opt           # optimise hot code, re-profile, verify
build/wattwise benchmarks/hoist.cpp --ir --cfg      # inspect IR and CFG
build/wattwise benchmarks/matmul.cpp --dot out/     # Graphviz energy heat-map
build/wattwise benchmarks/hoist.cpp --opt --json r.json
```

Run `build/wattwise --help` for every option. Exit codes: 0 ok, 1 compile
error, 2 runtime error, 3 optimisation changed program output, 64 usage error.

## Layout

| Path | Contents | Owner |
|---|---|---|
| `src/lexer.cpp` `parser.cpp` `semantic.cpp` | DFA scanner, recursive-descent parser with panic-mode recovery, scoped symbol table + type checker | M1 |
| `src/ir.cpp` `irgen.cpp` `cfg.cpp` `energy_model.cpp` | quadruple IR, syntax-directed translation, CFG/dominators/loops/liveness, cost table | M2 |
| `src/analysis.cpp` `src/optimize/` | energy aggregation, hotspot ranking, strength reduction, extended-block CSE, LICM, redundant-load elimination, dead-code elimination | M3 |
| `src/frequency.cpp` | static trip-count and call-graph frequency | M3 · M4 |
| `src/interpreter.cpp` `report.cpp` `main.cpp` | dynamic profiler, reports (text/JSON/DOT), CLI | M4 |
| `benchmarks/` | 15 benchmark programs + g++ golden outputs | M2 · M4 |
| `tests/unit/` | Catch2 unit tests (58 cases) | all |
| `tests/valid/`, `tests/invalid/` | 20 extra valid programs, 33 invalid programs | M1 · M4 |
| `scripts/` | `difftest.sh`, `invalidtest.sh` | M4 |
| `docs/` | `subset.md` (grammar), `cost_model.md`, `ir_format.md` | M1 · M2 |

## Web UI

A local browser front-end wraps the compiled CLI (paste or load a program, toggle
optimisation / dynamic profiling, view charts, hotspot and loop tables, and the
list of rewrites). It needs Node.js 18+ and a built `build/wattwise`:

```sh
cd webapp && npm install && npm start      # then open http://localhost:5173
```

## Status at Review 3

* Front-end, IR, CFG, cost model, static + dynamic frequency, analysis, reporting: complete.
* Passes: strength reduction, common-subexpression elimination over extended basic
  blocks, LICM, redundant-load elimination (RLE) and dead-code elimination (DCE).
  `--opt` runs `sr,cse,licm,cse,rle,dce`; any subset can be chosen with `--opt=LIST`.
* 35/35 valid programs match g++ output before and after optimisation (`--opt-all`);
  33/33 invalid programs rejected in the correct phase; 58/58 unit tests pass.
* Energy reduction under the default profile-directed `--opt` (static model):
  7 of the 14 original benchmarks improve (mean 7.7%, up from 5 of 14 and 6.2% before
  RLE/DCE); the new `redundant` benchmark improves by 52.9%.

## Known limitations

* The cost model is a relative table (memory ≫ divide > multiply > ALU > branch), not
  calibrated against hardware, and has no cache, pipeline or branch-prediction model.
  `--costs FILE` replaces any entry, so a measured table can be dropped in.
* RLE works within extended basic blocks (a block with one predecessor inherits its
  predecessor's knowledge). It does not forward values across loop iterations, so
  `prefix_sum` (`pre[i] = pre[i-1] + ...`) is unchanged.
* Static trip counts are exact only for loops with a constant start, step and bound;
  other loops fall back to heuristics (see `docs/cost_model.md`).
* The accepted language is a small C++ subset (`docs/subset.md`): no classes, templates,
  pointers or input.
