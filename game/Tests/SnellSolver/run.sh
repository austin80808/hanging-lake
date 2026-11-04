#!/bin/bash
# Build and run the Snell solver tests without Unreal: the real HLSnellSolver.cpp
# compiled against a small Core shim. Needs clang++ (or g++) with C++20.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
src="$here/../../Source/HangingLake"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
"${CXX:-clang++}" -std=c++20 -Wall -Wextra -Werror -Wno-unused-parameter \
  -I"$here/shim" -I"$src" \
  "$src/HLSnellSolver.cpp" "$here/test_snell_solver.cpp" -o "$out/test_snell_solver"
"$out/test_snell_solver"
