#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${TMPDIR:-/tmp}/rsch_core_tests"
g++ -std=c++20 -Wall -Wextra -Wpedantic -I"$ROOT/include" "$ROOT/src/core.cpp" "$ROOT/tests/test_core.cpp" -o "$OUT"
"$OUT"
