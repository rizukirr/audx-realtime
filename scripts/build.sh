#!/usr/bin/env bash

set -euo pipefail

BUILD_TYPE="${1:-debug}"
BUILD_TYPE="${BUILD_TYPE,,}" # lowercase it

case "$BUILD_TYPE" in
release)
  echo "Building in Release mode..."
  cmake -S . -B build/release \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang
  cmake --build build/release --clean-first
  ;;

debug)
  echo "Building in Debug mode..."
  cmake -S . -B build/debug \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang
  cmake --build build/debug --clean-first
  ;;
*)
  echo "Usage: $0 [debug|release|android]"
  exit 1
  ;;
esac
