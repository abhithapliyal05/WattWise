# WattWise — Compiler-Assisted Energy Profiling and Optimization

BCSE307 Compiler Design · Team A35 · Review 2 build (v0.2.0)

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
| `src/analysis.cpp` `src/optimize/` | energy aggregation, hotspot ranking, strength reduction, local CSE, LICM | M3 |
| `src/frequency.cpp` | static trip-count and call-graph frequency | M3 · M4 |
| `src/interpreter.cpp` `report.cpp` `main.cpp` | dynamic profiler, reports (text/JSON/DOT), CLI | M4 |
| `benchmarks/` | 14 benchmark programs + g++ golden outputs | M2 · M4 |
| `tests/unit/` | Catch2 unit tests (42 cases) | all |
| `tests/valid/`, `tests/invalid/` | 20 extra valid programs, 33 invalid programs | M1 · M4 |
| `scripts/` | `difftest.sh`, `invalidtest.sh` | M4 |
| `docs/` | `subset.md` (grammar), `cost_model.md`, `ir_format.md` | M1 · M2 |

## Status at Review 2

* Front-end, IR, CFG, cost model, static + dynamic frequency, analysis, reporting: complete.
* Passes: strength reduction, local CSE, LICM complete. Dead-code elimination and
  redundant-load elimination are planned for Review 3.
* 34/34 valid programs match g++ output before and after optimisation;
  33/33 invalid programs rejected in the correct phase; 42/42 unit tests pass.
