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
  cmake --build build --clean-first
  ;;

debug)
  echo "Building in Debug mode..."
  cmake -S . -B build/debug \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang
  cmake --build build --clean-first
  ;;

android)
  echo "Building for Android..."
  NDK_PATH="${ANDROID_HOME}/ndk/29.0.14206865" # adjust your version here
  ABI="arm64-v8a"
  API=21

  if [[ ! -d "$NDK_PATH" ]]; then
    echo "NDK path not found at: $NDK_PATH"
    exit 1
  fi

  cmake -S . -B build/android \
    -DCMAKE_TOOLCHAIN_FILE="$NDK_PATH/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM="android-$API" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="${PWD}/build/android/libs/$ABI"

  cmake --build build/android -j"$(nproc)"
  echo "Android build done: build/android/libs/$ABI/libaudx_src.so"
  ;;

*)
  echo "Usage: $0 [debug|release|android]"
  exit 1
  ;;
esac
