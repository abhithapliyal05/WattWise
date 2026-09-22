#!/usr/bin/env bash
# difftest.sh — differential testing over benchmarks/*.cpp.
# For every benchmark, three executions must agree on stdout and exit code:
#   (1) the original program compiled by g++ -O0      (ground truth)
#   (2) the unoptimised WattWise IR in the interpreter  (front-end + IR correct)
#   (3) the optimised WattWise IR in the interpreter    (passes preserve semantics)
# It also stores the expected output as benchmarks/<name>.expected.
# usage: scripts/difftest.sh path/to/wattwise
set -u
WW=${1:-build/wattwise}
CXX=${CXX:-g++}
TMP=$(mktemp -d)
pass=0; fail=0
printf "%-18s %-8s %-8s %-8s\n" "benchmark" "g++" "IR" "IR-opt"
for src in benchmarks/*.cpp tests/valid/*.cpp; do
  name=$(basename "$src" .cpp)
  if ! $CXX -std=c++17 -O0 -w -o "$TMP/$name" "$src" 2> "$TMP/$name.gxx.err"; then
    printf "%-18s %-8s\n" "$name" "NOCOMPILE"; cat "$TMP/$name.gxx.err"; fail=$((fail+1)); continue
  fi
  "$TMP/$name" > "$TMP/$name.gold"; gold_rc=$?
  "$WW" "$src" --run > "$TMP/$name.ir" 2> "$TMP/$name.ir.err"; ir_rc=$?
  "$WW" "$src" --run-opt --opt-all > "$TMP/$name.opt" 2> "$TMP/$name.opt.err"; opt_rc=$?
  r1=FAIL; r2=FAIL
  cmp -s "$TMP/$name.gold" "$TMP/$name.ir" && [ $gold_rc -eq $ir_rc ] && r1=ok
  cmp -s "$TMP/$name.gold" "$TMP/$name.opt" && [ $gold_rc -eq $opt_rc ] && r2=ok
  printf "%-18s %-8s %-8s %-8s\n" "$name" "rc=$gold_rc" "$r1" "$r2"
  cp "$TMP/$name.gold" "${src%.cpp}.expected"
  if [ $r1 = ok ] && [ $r2 = ok ]; then pass=$((pass+1)); else
    fail=$((fail+1)); diff "$TMP/$name.gold" "$TMP/$name.ir" | head -5; cat "$TMP/$name.ir.err" "$TMP/$name.opt.err"; fi
done
rm -rf "$TMP"
echo "differential: $pass passed, $fail failed"
[ $fail -eq 0 ]
