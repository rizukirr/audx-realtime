#!/bin/bash
#
# Deploy audx-realtime libraries to AudxAndroid project
#
# Usage:
#   ./scripts/deploy-to-android.sh [android-project-path]
#
# If no path is provided, defaults to ~/Projects/AudxAndroid

set -e  # Exit on error

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Default Android project path
DEFAULT_ANDROID_PROJECT="$HOME/Projects/AudxAndroid"
ANDROID_PROJECT="${1:-$DEFAULT_ANDROID_PROJECT}"

# Paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
INCLUDE_DIR="$PROJECT_ROOT/include/audx"

# Android project paths
ANDROID_JNILIBS="$ANDROID_PROJECT/app/src/main/jniLibs"
ANDROID_INCLUDE="$ANDROID_PROJECT/app/src/main/cpp/include/audx"

echo -e "${BLUE}=== Audx Realtime Android Deployment ===${NC}\n"

# Verify Android project exists
if [ ! -d "$ANDROID_PROJECT" ]; then
    echo -e "${RED}Error: Android project not found at: $ANDROID_PROJECT${NC}"
    echo "Usage: $0 [android-project-path]"
    exit 1
fi

# Verify build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${RED}Error: Build directory not found. Please run ./scripts/build.sh android first.${NC}"
    exit 1
fi

# Create jniLibs directories if they don't exist
echo -e "${YELLOW}Creating jniLibs directories...${NC}"
mkdir -p "$ANDROID_JNILIBS"/{arm64-v8a,armeabi-v7a,x86_64,x86}

# Create include directory if it doesn't exist
mkdir -p "$ANDROID_INCLUDE"

# Copy libraries for each ABI
echo -e "\n${YELLOW}Copying native libraries...${NC}"

ABIS=("arm64-v8a" "armeabi-v7a" "x86_64" "x86")
for ABI in "${ABIS[@]}"; do
    SRC_LIB="$BUILD_DIR/android-$ABI/lib/libaudx_src.so"
    DST_DIR="$ANDROID_JNILIBS/$ABI"

    if [ -f "$SRC_LIB" ]; then
        cp "$SRC_LIB" "$DST_DIR/"
        SIZE=$(du -h "$SRC_LIB" | cut -f1)
        echo -e "${GREEN}✓${NC} Copied $ABI library ($SIZE)"
    else
        echo -e "${YELLOW}⚠${NC} Warning: $ABI library not found at $SRC_LIB"
    fi
done

# Copy header files
echo -e "\n${YELLOW}Copying header files...${NC}"
if [ -d "$INCLUDE_DIR" ]; then
    cp -r "$INCLUDE_DIR"/* "$ANDROID_INCLUDE/"
    HEADER_COUNT=$(find "$INCLUDE_DIR" -name "*.h" | wc -l)
    echo -e "${GREEN}✓${NC} Copied $HEADER_COUNT header file(s)"
else
    echo -e "${YELLOW}⚠${NC} Warning: Include directory not found at $INCLUDE_DIR"
fi

# Summary
echo -e "\n${BLUE}=== Deployment Summary ===${NC}"
echo "Android Project: $ANDROID_PROJECT"
echo "Libraries deployed to: $ANDROID_JNILIBS"
echo "Headers deployed to: $ANDROID_INCLUDE"

echo -e "\n${GREEN}Deployment completed successfully!${NC}"
echo -e "\nNext steps:"
echo "  cd $ANDROID_PROJECT"
echo "  ./gradlew assembleDebug"
