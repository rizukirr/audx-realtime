#!/bin/bash

set -e

# Check ANDROID_HOME
if [ -z "$ANDROID_HOME" ]; then
  echo -e "Error: ANDROID_HOME environment variable not set"
  exit 1
fi

# NDK configuration
NDK_VERSION="30.0.14904198"
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

# Android ABIs to build
ABIS=("arm64-v8a" "armeabi-v7a" "x86_64")

# Optional ABI filter: $2 narrows the build to a single ABI.
if [ -n "$2" ]; then
  REQUESTED_ABI="$2"
  MATCHED=0
  for ABI in "${ABIS[@]}"; do
    if [ "$ABI" = "$REQUESTED_ABI" ]; then
      MATCHED=1
      break
    fi
  done
  if [ "$MATCHED" -ne 1 ]; then
    echo "Error: Unsupported ABI '${REQUESTED_ABI}'"
    echo "Supported ABIs: ${ABIS[*]}"
    exit 1
  fi
  ABIS=("$REQUESTED_ABI")
fi

echo -e "Building for Android ABIs: ${ABIS[*]} (${BUILD_TYPE})..."

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

  # Build shared lib (for JNI consumers) + bundled static lib (for KMP cinterop)
  cmake --build "build/android-${ABI}" --target audx_src audx_bundled -j$(nproc)

  # Copy shared lib output
  mkdir -p "libs/${ABI}"
  cp "build/android-${ABI}/lib/libaudx_src.so" "libs/${ABI}/"

  # Copy bundled static lib for Kotlin/Native cinterop
  mkdir -p "libs-static/${ABI}"
  cp "build/android-${ABI}/lib/libaudx.a" "libs-static/${ABI}/"

  # Strip symbols to reduce size (30-40% reduction, no performance impact)
  echo -e "Stripping symbols from ${ABI} library..."
  ${NDK_PATH}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip "libs/${ABI}/libaudx_src.so"

  # Assert NEON for armeabi-v7a — armv7 NEON depends on the NDK's -mfpu=neon
  # default; guard against a silent regression if a future NDK changes it.
  if [ "$ABI" = "armeabi-v7a" ]; then
    if ! ${NDK_PATH}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf -A "libs/${ABI}/libaudx_src.so" | grep -q "Advanced_SIMD_arch"; then
      echo "Error: NEON not enabled in libs/${ABI}/libaudx_src.so (Advanced_SIMD_arch attribute missing)"
      exit 1
    fi
  fi

  echo -e "${GREEN}✓ ${ABI} build complete${NC}"
}

# Build for each ABI
for ABI in "${ABIS[@]}"; do
  build_abi "$ABI"
done

echo -e "All ABIs built successfully!"
