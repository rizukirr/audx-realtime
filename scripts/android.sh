#!/bin/bash

set -e

# Check ANDROID_HOME
if [ -z "$ANDROID_HOME" ]; then
  echo -e "Error: ANDROID_HOME environment variable not set"
  exit 1
fi

# NDK configuration
NDK_VERSION="29.0.14206865"
NDK_PATH="${ANDROID_HOME}/ndk/${NDK_VERSION}"
API=24

# Check NDK
if [ ! -d "$NDK_PATH" ]; then
  echo -e "Error: NDK not found at: $NDK_PATH"
  exit 1
fi

# Color codes
GREEN='\033[0;32m'
NC='\033[0m' # No Color

# Parse build type argument
if [ -z "$1" ]; then
  echo "Usage: $0 <build-type>"
  echo
  echo "Build types:"
  echo "  debug"
  echo "  release"
  exit 1
fi

# Map build type to CMake format
case "$1" in
  debug)
    BUILD_TYPE="Debug"
    ;;
  release)
    BUILD_TYPE="Release"
    ;;
  *)
    echo "Error: Invalid build type '$1'"
    echo "Valid options: debug, release"
    exit 1
    ;;
esac

# Android ABIs to build (64-bit only)
ABIS=("arm64-v8a" "x86_64")

echo -e "Building for multiple Android ABIs (${BUILD_TYPE})..."

# Function to build for a specific ABI
build_abi() {
  local ABI=$1
  echo -e "Building for ${ABI}..."

  # Configure CMake
  cmake -S . -B "build/android-${ABI}" \
    -DCMAKE_TOOLCHAIN_FILE="${NDK_PATH}/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="${ABI}" \
    -DANDROID_PLATFORM="android-${API}" \
    -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="build/android/libs/${ABI}"

  # Build (only library targets, skip tests)
  cmake --build "build/android-${ABI}" --target audx_src -j$(nproc)

  # Copy output
  mkdir -p "libs/${ABI}"
  cp "build/android-${ABI}/lib/libaudx_src.so" "libs/${ABI}/"

  # Strip symbols to reduce size (30-40% reduction, no performance impact)
  echo -e "Stripping symbols from ${ABI} library..."
  ${NDK_PATH}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip "libs/${ABI}/libaudx_src.so"

  echo -e "${GREEN}✓ ${ABI} build complete${NC}"
}

# Build for each ABI
for ABI in "${ABIS[@]}"; do
  build_abi "$ABI"
done

echo -e "All ABIs built successfully!"
