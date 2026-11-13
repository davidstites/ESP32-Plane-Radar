#!/usr/bin/env bash
# Run host unit tests and enforce 100% line coverage of lib/adsb_parse.
set -euo pipefail
cd "$(dirname "$0")/.."

find .pio/build/native -name '*.gcda' -delete 2>/dev/null || true
pio test -e native

gcov_args=()
if [[ "$(uname)" == "Darwin" ]]; then
  gcov_args=(--gcov-executable "xcrun llvm-cov gcov")
fi

gcovr --root . --filter 'lib/adsb_parse/' "${gcov_args[@]}" \
  --fail-under-line 100 --print-summary --txt-metric line .pio/build/native
