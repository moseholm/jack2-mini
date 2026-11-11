#!/usr/bin/env bash
# The build and test steps of .github/workflows/build.yml. The workflow runs this script, so a local run and a CI run
# take the same steps with the same flags.
#
#   scripts/ci.sh configure|build|test|install
#
# Environment:
#   CC, CXX         the compilers (default: the system ones)
#   GENERATOR       the CMake generator (default: the CMake default, Make)
#   BUILD_TYPE      the CMake build type (default: Release). Debug keeps the asserts enabled.
#   EXTRA_FLAGS     extra C and C++ flags, e.g. "-msse4.1 -mavx2" for the SIMD kernels
#   BUILD_DIR       the build directory (default: build-ci)
#   DESTDIR         the install root (default: build-ci/destdir)
set -euo pipefail
cd "$(dirname "$0")/.."

step=${1:?usage: scripts/ci.sh configure|build|test|install}
build_dir=${BUILD_DIR:-build-ci}
jobs=$(nproc)

case $step in
configure)
    # shellcheck disable=SC2086
    cmake -S . -B "$build_dir" ${GENERATOR:+-G "$GENERATOR"} \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}" -DJACK_BUILD_TESTS=ON \
        ${EXTRA_FLAGS:+-DCMAKE_C_FLAGS="$EXTRA_FLAGS" -DCMAKE_CXX_FLAGS="$EXTRA_FLAGS"}
    ;;
build)
    cmake --build "$build_dir" --parallel "$jobs"
    ;;
test)
    ctest --test-dir "$build_dir" --output-on-failure
    ;;
install)
    DESTDIR="${DESTDIR:-$build_dir/destdir}" cmake --install "$build_dir"
    ;;
*)
    echo "unknown step: $step" >&2
    exit 2
    ;;
esac
