cmake_minimum_required(VERSION 3.10)
project(audx_realtime VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_FLAGS_DEBUG "-Wall -Wextra -g -Og")
# Note: -march=native is NOT used here to allow Android cross-compilation
# Platform-specific optimizations are set per-target below
if(ANDROID)
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
else()
    # Desktop builds can use -march=native
    set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -march=native -DNDEBUG")
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Allow choosing shared or static via -DBUILD_SHARED_LIBS
option(BUILD_SHARED_LIBS "Build shared instead of static libraries" ON)

# Build RNNoise from source
if(EXISTS "${CMAKE_SOURCE_DIR}/external/rnnoise/src/rnn.c")
    add_library(rnnoise STATIC
        ${CMAKE_SOURCE_DIR}/external/rnnoise/src/denoise.c
        ${CMAKE_SOURCE_DIR}/external/rnnoise/src/rnn.c
        ${CMAKE_SOURCE_DIR}/external/rnnoise/src/pitch.c
        ${CMAKE_SOURCE_DIR}/external/rnnoise/src/kiss_fft.c
        ${CMAKE_SOURCE_DIR}/external/rnnoise/src/celt_lpc.c
        ${CMAKE_SOURCE_DIR}/external/rnnoise/src/nnet.c
        ${CMAKE_SOURCE_DIR}/external/rnnoise/src/nnet_default.c
        ${CMAKE_SOURCE_DIR}/external/rnnoise/src/parse_lpcnet_weights.c
        ${CMAKE_SOURCE_DIR}/external/rnnoise/src/rnnoise_tables.c
        ${CMAKE_SOURCE_DIR}/external/rnnoise/src/rnnoise_data.c
    )
    target_include_directories(rnnoise PUBLIC
        ${CMAKE_SOURCE_DIR}/external/rnnoise/include
        ${CMAKE_SOURCE_DIR}/external/rnnoise/src
    )

    # Enable PIC for static library when building shared libraries
    set_target_properties(rnnoise PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # RNNoise compile definitions
    target_compile_definitions(rnnoise PRIVATE
        COMPILE_OPUS
        _FORTIFY_SOURCE=0
    )

    # Enable x86 SIMD optimizations
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)" OR
       ANDROID_ABI STREQUAL "x86" OR ANDROID_ABI STREQUAL "x86_64")

        if(NOT ANDROID)
            # Desktop builds can use -march=native for auto-detection
            target_compile_options(rnnoise PRIVATE -march=native)
        endif()

        # Add x86 SIMD source files
        target_sources(rnnoise PRIVATE
            ${CMAKE_SOURCE_DIR}/external/rnnoise/src/x86/x86cpu.c
            ${CMAKE_SOURCE_DIR}/external/rnnoise/src/x86/x86_dnn_map.c
            ${CMAKE_SOURCE_DIR}/external/rnnoise/src/x86/nnet_sse4_1.c
            ${CMAKE_SOURCE_DIR}/external/rnnoise/src/x86/nnet_avx2.c
        )

        # Set specific optimization flags for SIMD files
        set_source_files_properties(
            ${CMAKE_SOURCE_DIR}/external/rnnoise/src/x86/nnet_sse4_1.c
            PROPERTIES COMPILE_FLAGS "-msse4.1"
        )
        set_source_files_properties(
            ${CMAKE_SOURCE_DIR}/external/rnnoise/src/x86/nnet_avx2.c
            PROPERTIES COMPILE_FLAGS "-mavx2 -mfma"
        )
    endif()

    # Enable ARM NEON optimization for Android and ARM platforms
    if(ANDROID)
        if(ANDROID_ABI STREQUAL "armeabi-v7a")
            # 32-bit ARM requires explicit NEON flag
            target_compile_options(rnnoise PRIVATE -mfpu=neon)
            message(STATUS "Enabled ARM NEON for armeabi-v7a")
        elseif(ANDROID_ABI STREQUAL "arm64-v8a")
            # arm64-v8a: NEON is always available, no flag needed
            message(STATUS "ARM64 NEON enabled by default for arm64-v8a")
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)")
        # Native ARM builds (non-Android)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
            message(STATUS "ARM64 NEON enabled by default")
        else()
            target_compile_options(rnnoise PRIVATE -mfpu=neon)
            message(STATUS "Enabled ARM NEON for 32-bit ARM")
        endif()
    endif()

    set(HAVE_RNNOISE TRUE)
    message(STATUS "Building RNNoise from source")
else()
    message(FATAL_ERROR "RNNoise not found. Run: git submodule update --init")
endif()

# Source files
file(GLOB_RECURSE SRC_FILES "src/audx/*.c")

# Define the library (shared or static depends on BUILD_SHARED_LIBS)
add_library(audx_src ${SRC_FILES})
target_include_directories(audx_src PUBLIC
    include/audx
    ${CMAKE_SOURCE_DIR}/external/rnnoise/include
)

# Link RNNoise
if(ANDROID)
    target_link_libraries(audx_src PUBLIC rnnoise m log)
else()
    target_link_libraries(audx_src PUBLIC rnnoise m)
endif()

# Version define
target_compile_definitions(audx_src PUBLIC AUDX_VERSION="${PROJECT_VERSION}")

# Enable SIMD optimizations for audx_src
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)" OR
   ANDROID_ABI STREQUAL "x86" OR ANDROID_ABI STREQUAL "x86_64")
    if(NOT ANDROID)
        # Desktop builds: use -march=native
        target_compile_options(audx_src PRIVATE -march=native)
    else()
        # Android x86/x86_64: explicitly enable SSE4.1
        target_compile_options(audx_src PRIVATE -msse4.1)
    endif()
endif()

# Enable ARM NEON for audx_src
if(ANDROID)
    if(ANDROID_ABI STREQUAL "armeabi-v7a")
        # 32-bit ARM requires explicit NEON flag
        target_compile_options(audx_src PRIVATE -mfpu=neon)
    endif()
    # arm64-v8a has NEON by default
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)")
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        target_compile_options(audx_src PRIVATE -mfpu=neon)
    endif()
endif()

if(ANDROID)
    message(STATUS "Building for Android...")
    target_compile_definitions(audx_src PUBLIC AUDX_ANDROID=1)
else()
    message(STATUS "Building for desktop...")
endif()

# Executable (desktop only)
if(NOT ANDROID)
    add_executable(audx_realtime src/main.c)
    target_link_libraries(audx_realtime PRIVATE audx_src)
    install(TARGETS audx_realtime DESTINATION bin)
endif()

enable_testing()
add_subdirectory(tests)
