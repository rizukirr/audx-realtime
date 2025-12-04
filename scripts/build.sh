#!/bin/bash

# This script builds the project.

set -e

if [ -z "$1" ]; then
  echo "Usage: $0 <build-type>"
  echo
  echo "Build types:"
  echo "  debug"
  echo "  release"
  exit 1
fi

build_type=$1

case $build_type in
debug)
  echo "Building debug"
  cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang
  cmake --build build/debug
  ;;
release)
  echo "Building release"
  cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang
  cmake --build build/release
  ;;
*)
  echo "Unknown build type: $build_type"
  exit 1
  ;;
esac
