#!/usr/bin/env bash
# invalidtest.sh — every program in tests/invalid/ must be REJECTED (exit 1)
# in the phase named by its "// EXPECT: <lexical|syntax|semantic>" header,
# and must NOT crash. Also confirms g++ rejects it too, or flags it as a
# subset restriction (valid C++ that WattWise deliberately does not accept).
# usage: scripts/invalidtest.sh path/to/wattwise
set -u
WW=${1:-build/wattwise}
CXX=${CXX:-g++}
pass=0; fail=0
printf "%-28s %-9s %-9s %-6s %s\n" "program" "expect" "got" "rc" "g++"
for src in tests/invalid/*.cpp; do
  name=$(basename "$src" .cpp)
  want=$(sed -n 's#^// EXPECT: *\([a-z]*\).*#\1#p' "$src" | head -1)
  err=$("$WW" "$src" --quiet 2>&1 >/dev/null); rc=$?
  got=$(printf '%s\n' "$err" | sed -n 's/.* \([a-z]*\) error(s); compilation stopped.*/\1/p' | head -1)
  if $CXX -std=c++17 -fsyntax-only -w "$src" 2>/dev/null; then gxx="accepts (subset rule)"; else gxx="rejects"; fi
  if [ $rc -eq 1 ] && [ "$got" = "$want" ]; then r=ok; pass=$((pass+1)); else r=FAIL; fail=$((fail+1)); fi
  printf "%-28s %-9s %-9s %-6s %s %s\n" "$name" "$want" "${got:-none}" "$rc" "$gxx" "$([ $r = FAIL ] && echo '<-- FAIL')"
done
echo "invalid programs: $pass rejected correctly, $fail failed"
[ $fail -eq 0 ]
