# syntax=docker/dockerfile:1.7
#
# Commit-image Dockerfile for gdk-pixbuf (GNOME's image-loading library, C/meson).
# Builds ON TOP of the org C/C++ base image (ghcr.io/savantenvs/base), which already
# provides debian + clang/llvm + libFuzzer/afl++ + the build-contract ENV.
#
# Build (from the repo root):
#   DOCKER_BUILDKIT=1 docker build -f mayhem/Dockerfile \
#       --build-arg COMMIT=$(git rev-parse HEAD) \
#       [--build-arg SOURCE_DATE_EPOCH=$(date +%s)] \
#       -t gdk-pixbuf-commit:test .

FROM ghcr.io/savantenvs/base:20260621

ARG COMMIT
ARG REPO_URL
# SOURCE_DATE_EPOCH stays an ARG (available to build.sh); do NOT bake it as an ENV.
ARG SOURCE_DATE_EPOCH

# Sanitizer override: base exports SANITIZER_FLAGS (ASan+UBSan, halting) as default.
# Declared WITHOUT a default so the base ENV flows through when not passed.
ARG SANITIZER_FLAGS
ARG COVERAGE_FLAGS=
ARG MAYHEM_JOBS=

# gdk-pixbuf's own build system (meson) + the format libraries its builtin loaders need
# (glib/gobject/gio, libjpeg, libpng) + gettext for its i18n build step. Re-pin to
# SOURCE_DATE_EPOCH first for reproducibility (repro-sources-list.sh ships in the base).
USER root
RUN [ -n "$SOURCE_DATE_EPOCH" ] && repro-sources-list.sh || true ; \
    apt-get update && apt-get install -yq --no-install-recommends \
        meson ninja-build pkg-config gettext \
        libglib2.0-dev libjpeg62-turbo-dev libpng-dev \
    && rm -rf /var/lib/apt/lists/*
USER mayhem

# Source enters from the build CONTEXT (the CI checkout): the FULL repo (incl. .git
# history) is copied to /mayhem as `mayhem`; build.sh builds in place.
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

RUN bash mayhem/build.sh

RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh — repo ships no functional tests"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
