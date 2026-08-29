#!/usr/bin/env bash
#
# mayhem/build.sh — build gdk-pixbuf + its 5 fuzz harnesses + the KAT probe + the project's
# own meson test suite (oracle).
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem. Uses the base
# image's build contract (CC, CXX, LIB_FUZZING_ENGINE, SANITIZER_FLAGS, DEBUG_FLAGS,
# STANDALONE_FUZZ_MAIN, SRC) — see mayhem-repo-integration/mayhem/build.sh for the full
# contract doc.
#
# Shape: TWO independent meson builds off the same source tree (the "dual build is usually
# free" pattern — meson writes to its own builddir, so no make-clean/stash dance needed):
#   mayhem-build/   sanitized + fuzzer-no-link static lib -> links the 5 fuzz harnesses
#                   (+ 5 standalone reproducers)
#   mayhem-oracle/  clean, NORMAL flags static lib + the project's own `tests/` suite
#                   (built with -Dtests=true) -> mayhem/test.sh only RUNS this, never builds.
# Both builds `meson install` into their own --prefix so harnesses/probe pick up headers +
# a pkg-config file via `pkg-config --static gdk-pixbuf-2.0` (mirrors OSS-Fuzz's own
# projects/gdk-pixbuf/build.sh PKG_CONFIG_PATH approach) instead of guessing include paths.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# Loader/feature set shared by both builds: mirrors google/oss-fuzz's own
# projects/gdk-pixbuf/build.sh flags (a proven-working configuration for fuzzing this
# library) — all format loaders compiled BUILTIN (no runtime module loading / no
# gdk-pixbuf-query-loaders.cache dance), tiff disabled (keeps the dep footprint to
# apt's libjpeg/libpng — no libtiff-dev needed), introspection/docs/man/thumbnailer/
# glycin/android off (not exercised by these harnesses; smaller & faster build).
# gio_sniffing disabled: our harnesses always call the loader explicitly and don't need
# GIO's shared-mime-info-backed content sniffing (which would need that DB installed).
MESON_LOADER_OPTS=(
  -Dintrospection=disabled
  -Dman=false
  -Ddocumentation=false
  -Dinstalled_tests=false
  -Dthumbnailer=disabled
  -Dglycin=disabled
  -Dandroid=disabled
  -Dgio_sniffing=false
  -Dbuiltin_loaders=all
  -Djpeg=enabled
  -Dpng=enabled
  -Dgif=enabled
  -Dothers=enabled
  -Dtiff=disabled
  --default-library=static
  --libdir=lib
)

# ---------------------------------------------------------------------------
# 1) SANITIZED build: the project itself instrumented for fuzzing.
#    -fsanitize=fuzzer-no-link is appended UNCONDITIONALLY (even when $SANITIZER_FLAGS is
#    empty) so the fuzzed library code always carries SanCov coverage instrumentation —
#    otherwise a harness can build+smoke-pass locally yet record 0 edges under Mayhem.
# ---------------------------------------------------------------------------
rm -rf "$SRC/mayhem-build"
CC="$CC" CFLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link" \
  meson setup --prefix="$SRC/mayhem-build/prefix" "${MESON_LOADER_OPTS[@]}" -Dtests=false \
  "$SRC/mayhem-build/build" "$SRC"
ninja -C "$SRC/mayhem-build/build" -j"$MAYHEM_JOBS"
ninja -C "$SRC/mayhem-build/build" install

# ---------------------------------------------------------------------------
# 2) ORACLE build: the project's NORMAL (unsanitized) flags + its own `tests/` suite, so
#    mayhem/test.sh only RUNS what's built here — never compiles.
# ---------------------------------------------------------------------------
rm -rf "$SRC/mayhem-oracle"
CC="$CC" CFLAGS="$COVERAGE_FLAGS" \
  meson setup --prefix="$SRC/mayhem-oracle/prefix" "${MESON_LOADER_OPTS[@]}" -Dtests=true \
  "$SRC/mayhem-oracle/build" "$SRC"
ninja -C "$SRC/mayhem-oracle/build" -j"$MAYHEM_JOBS"
ninja -C "$SRC/mayhem-oracle/build" install

# ---------------------------------------------------------------------------
# 3) Fuzz harnesses (fuzzer + standalone reproducer, x5) against the SANITIZED build.
# ---------------------------------------------------------------------------
export PKG_CONFIG_PATH="$SRC/mayhem-build/prefix/lib/pkgconfig"
FUZZ_CFLAGS=$(pkg-config --static --cflags gdk-pixbuf-2.0)
FUZZ_LIBS=$(pkg-config --static --libs gdk-pixbuf-2.0)

for t in animation_fuzzer pixbuf_cons_fuzzer pixbuf_file_fuzzer pixbuf_scale_fuzzer stream_fuzzer; do
  src="$SRC/mayhem/targets/${t}.c"
  echo "== building $t =="
  # shellcheck disable=SC2086
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link $LIB_FUZZING_ENGINE \
      -I"$SRC/mayhem/targets" $FUZZ_CFLAGS \
      "$src" $FUZZ_LIBS \
      -o "/mayhem/${t}"
  # shellcheck disable=SC2086
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS \
      -I"$SRC/mayhem/targets" $FUZZ_CFLAGS \
      "$src" "$STANDALONE_FUZZ_MAIN" $FUZZ_LIBS \
      -o "/mayhem/${t}-standalone"
done

# ---------------------------------------------------------------------------
# 4) KAT probe against the ORACLE (clean, dynamically-linked) build — the behavioral oracle
#    mayhem/test.sh asserts an exact output line from.
# ---------------------------------------------------------------------------
export PKG_CONFIG_PATH="$SRC/mayhem-oracle/prefix/lib/pkgconfig"
KAT_CFLAGS=$(pkg-config --static --cflags gdk-pixbuf-2.0)
KAT_LIBS=$(pkg-config --static --libs gdk-pixbuf-2.0)
# shellcheck disable=SC2086
$CC -O2 -g $KAT_CFLAGS "$SRC/mayhem/kat/gdk_pixbuf_kat.c" $KAT_LIBS -o /mayhem/gdk_pixbuf_kat
file /mayhem/gdk_pixbuf_kat | grep -q 'dynamically linked' \
  || { echo "FATAL: gdk_pixbuf_kat is not dynamically linked — sabotage check would be blind to it" >&2; exit 1; }

echo "build.sh: done. Targets: animation_fuzzer pixbuf_cons_fuzzer pixbuf_file_fuzzer pixbuf_scale_fuzzer stream_fuzzer"
