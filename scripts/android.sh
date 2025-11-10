#!/usr/bin/env bash

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check ANDROID_HOME
if [ -z "$ANDROID_HOME" ]; then
  echo -e "${RED}Error: ANDROID_HOME environment variable not set${NC}"
  exit 1
fi

# NDK configuration
NDK_VERSION="29.0.14206865"
NDK_PATH="${ANDROID_HOME}/ndk/${NDK_VERSION}"
API=21

# Check NDK
if [ ! -d "$NDK_PATH" ]; then
  echo -e "${RED}Error: NDK not found at: $NDK_PATH${NC}"
  exit 1
fi

# ABIs to build
ABIS=("arm64-v8a" "armeabi-v7a" "x86" "x86_64")

echo -e "${GREEN}Building audx-realtime for multiple Android ABIs...${NC}"

# Function to build for a specific ABI
build_abi() {
  local ABI=$1
  echo -e "${YELLOW}Building for ${ABI}...${NC}"

  # Configure CMake
  cmake -S . -B "build/android-${ABI}" \
    -DCMAKE_TOOLCHAIN_FILE="${NDK_PATH}/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="${ABI}" \
    -DANDROID_PLATFORM="android-${API}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="build/android/libs/${ABI}"

  # Build
  cmake --build "build/android-${ABI}" -j$(nproc)

  # Copy output
  mkdir -p "libs/${ABI}"
  cp "build/android-${ABI}/lib/libaudx_src.so" "libs/${ABI}/"

  echo -e "${GREEN}✓ ${ABI} build complete${NC}"
}

# Build for each ABI
for ABI in "${ABIS[@]}"; do
  build_abi "$ABI"
done

echo -e "${GREEN}All ABIs built successfully!${NC}"
echo -e "Libraries are in: libs/"
tree libs/ || ls -lR libs/
