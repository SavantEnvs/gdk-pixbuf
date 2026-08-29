#!/usr/bin/env bash
#
# mayhem/test.sh — RUN gdk-pixbuf's own functional tests (already built by mayhem/build.sh's
# oracle pass) PLUS a mandatory known-answer probe.
#
# Why both: `meson test` is an exit-code-only RUNNER — under the gate's sabotage shim
# (LD_PRELOAD _exit(0)s every non-system binary before it does anything), the individual test
# executables it launches would exit 0 having read nothing, and meson/ninja's own summary
# treats "child exited 0" as PASS. That is a proven false-green (see SPEC §6.3 / the pkgconf
# case in docs/netnew-worker-prompt.md §4). So the PASS/FAIL verdict here is NOT derived from
# meson test's exit code alone — it is gated on the KAT probe, which does an exact bash-level
# stdout comparison against a hardcoded expected value. When the probe binary is neutered it
# is killed before printing anything, so its stdout no longer contains the expected line and
# the comparison fails loudly.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

FAILED=0

# ---------------------------------------------------------------------------
# 1) The project's OWN meson test suite (informational + real-defect signal; not the sole
#    oracle — see header). Built by build.sh's oracle pass at mayhem-oracle/build.
# ---------------------------------------------------------------------------
[ -d "$SRC/mayhem-oracle/build" ] || { echo "FATAL: mayhem-oracle/build missing — build.sh did not run" >&2; exit 1; }

MESON_LOG=$(mktemp)
if meson test -C "$SRC/mayhem-oracle/build" --print-errorlogs >"$MESON_LOG" 2>&1; then
  MESON_RC=0
else
  MESON_RC=$?
fi
cat "$MESON_LOG"
# meson's own summary line: "Ok:     N   Fail:  N   ..."
MESON_OK=$(grep -oP 'Ok:\s*\K[0-9]+' "$MESON_LOG" | tail -1)
MESON_FAIL=$(grep -oP 'Fail:\s*\K[0-9]+' "$MESON_LOG" | tail -1)
MESON_SKIP=$(grep -oP 'Skipped:\s*\K[0-9]+' "$MESON_LOG" | tail -1)
MESON_OK="${MESON_OK:-0}"; MESON_FAIL="${MESON_FAIL:-0}"; MESON_SKIP="${MESON_SKIP:-0}"
if [ "$MESON_OK" -eq 0 ] && [ "$MESON_FAIL" -eq 0 ]; then
  echo "FATAL: could not parse a meson test summary (0 Ok, 0 Fail) — suite did not run" >&2
  FAILED=1
fi
if [ "$MESON_RC" -ne 0 ]; then
  echo "meson test: exit $MESON_RC ($MESON_FAIL failed of $((MESON_OK+MESON_FAIL+MESON_SKIP)))"
fi

# ---------------------------------------------------------------------------
# 2) MANDATORY known-answer probe — the actual behavioral oracle (see header). Unconditional:
#    a missing binary/fixture is a FAILURE, never a skip.
# ---------------------------------------------------------------------------
KAT_BIN="/mayhem/gdk_pixbuf_kat"
KAT_FIXTURE="/mayhem/mayhem/kat/fixture.png"
KAT_PASS=0
KAT_FAIL=0

if [ ! -x "$KAT_BIN" ]; then
  echo "FATAL: $KAT_BIN missing or not executable" >&2
  KAT_FAIL=$((KAT_FAIL+1))
elif [ ! -f "$KAT_FIXTURE" ]; then
  echo "FATAL: $KAT_FIXTURE missing" >&2
  KAT_FAIL=$((KAT_FAIL+1))
else
  KAT_OUT=$("$KAT_BIN" "$KAT_FIXTURE" 2>/tmp/kat.err); KAT_RC=$?
  echo "$KAT_OUT"
  cat /tmp/kat.err >&2 || true

  EXPECTED_FULL='KAT full: width=48 height=48 n_channels=4 has_alpha=1 checksum=3242462767409439741'
  EXPECTED_TRUNC='KAT truncated: error=1'

  if [ "$KAT_RC" -ne 0 ]; then
    echo "FAIL: gdk_pixbuf_kat exited $KAT_RC (expected 0)" >&2
    KAT_FAIL=$((KAT_FAIL+1))
  elif ! grep -qF "$EXPECTED_FULL" <<<"$KAT_OUT"; then
    echo "FAIL: KAT full-decode line did not match. expected: $EXPECTED_FULL" >&2
    KAT_FAIL=$((KAT_FAIL+1))
  else
    KAT_PASS=$((KAT_PASS+1))
  fi

  if ! grep -qF "$EXPECTED_TRUNC" <<<"$KAT_OUT"; then
    echo "FAIL: KAT truncated-decode line did not match. expected: $EXPECTED_TRUNC" >&2
    KAT_FAIL=$((KAT_FAIL+1))
  else
    KAT_PASS=$((KAT_PASS+1))
  fi
fi

TOTAL_PASSED=$((MESON_OK + KAT_PASS))
TOTAL_FAILED=$((MESON_FAIL + KAT_FAIL))
[ "$FAILED" -eq 0 ] || TOTAL_FAILED=$((TOTAL_FAILED + 1))

emit_ctrf "meson-test+kat-probe" "$TOTAL_PASSED" "$TOTAL_FAILED" "$MESON_SKIP"
