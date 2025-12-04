cmake_minimum_required(VERSION 3.23)
project(audx VERSION 0.0.1 LANGUAGES C)

set(CMAKE_C_FLAGS_DEBUG
   "-g -O0 -Wall -Wextra -Wpedantic -fsanitize=address -fno-omit-frame-pointer")

set(CMAKE_C_FLAGS_RELEASE "-g -O3")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-g -O2")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Build RNNoise from source
if(EXISTS ${CMAKE_SOURCE_DIR}/external/rnnoise)
    add_library(rnnoise STATIC
        ${CMAKE_SOURCE_DIR}/external/rnnoise/src/kiss_fft.c
        ${CMAKE_SOURCE_DIR}/external/rnnoise/src/denoise.c
        ${CMAKE_SOURCE_DIR}/external/rnnoise/src/rnn.c
        ${CMAKE_SOURCE_DIR}/external/rnnoise/src/pitch.c
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

    set_target_properties(rnnoise PROPERTIES POSITION_INDEPENDENT_CODE ON)

    if(NOT ANDROID)
        target_compile_options(rnnoise PRIVATE -march=native)
    endif()

    target_compile_definitions(rnnoise PUBLIC
        COMPILE_OPUS
    )

    set(HAVE_RNNOISE TRUE)
    message(STATUS "Building RNNoise from source")
else()
    message(FATAL_ERROR "RNNoise not found. Run: git submodule update --init")
endif()

if(EXISTS ${CMAKE_SOURCE_DIR}/external/speexdsp/libspeexdsp/resample.c)
    add_library(speexdsp STATIC
        ${CMAKE_SOURCE_DIR}/external/speexdsp/libspeexdsp/resample.c
    )

    target_include_directories(speexdsp PUBLIC
        ${CMAKE_SOURCE_DIR}/external/speexdsp/include
    )

    target_include_directories(speexdsp PRIVATE
        ${CMAKE_SOURCE_DIR}/external/speexdsp/include/speex
        ${CMAKE_SOURCE_DIR}/external/speexdsp/libspeexdsp
    )

    set_target_properties(speexdsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

    target_compile_options(speexdsp PRIVATE -Wno-sign-compare)

    target_compile_definitions(speexdsp PRIVATE
        OUTSIDE_SPEEX
        FLOATING_POINT
        EXPORT=
        RANDOM_PREFIX=lib
    )

    set(HAVE_SPEEXDSP TRUE)
    message(STATUS "Building SpeexDSP resampler from source")
else()
    message(FATAL_ERROR "SpeexDSP not found. Run: git submodule update --init")
endif()

file(GLOB_RECURSE SOURCES src/*.c)
add_library(audx_src SHARED ${SOURCES})
target_include_directories(audx_src PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/external/rnnoise/include
    ${CMAKE_SOURCE_DIR}/external/speexdsp/include/speex
)
target_link_libraries(audx_src PUBLIC rnnoise speexdsp m)

# SpeexDSP resampler needs these definitions to match the library build
target_compile_definitions(audx_src PRIVATE
    OUTSIDE_SPEEX
    RANDOM_PREFIX=lib
)

if(NOT ANDROID)
    add_executable(audx main.c)
    target_link_libraries(audx audx_src)
endif()

# JNI Support
if(ANDROID)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "LTO enabled for Android Release build")
    endif()

    if(EXISTS "${CMAKE_SOURCE_DIR}/external/rnnoise/src/rnn.c")
        # Enable x86_64 SIMD optimizations
        if(ANDROID_ABI STREQUAL "x86_64")
            target_compile_definitions(rnnoise PRIVATE
                RNN_ENABLE_X86_RTCD
                CPU_INFO_BY_C  # Use __get_cpuid_count intrinsic
            )
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
        elseif(ANDROID_ABI STREQUAL "arm64-v8a")
            # arm64-v8a: NEON is always available, no flag needed
            message(STATUS "ARM64 NEON enabled by default for arm64-v8a")
        else()
            message(FATAL_ERROR "Unsupported ABI: ${ANDROID_ABI}. Only arm64-v8a and x86_64 are supported.")
        endif()
    endif()


    if(EXISTS "${CMAKE_SOURCE_DIR}/external/speexdsp/libspeexdsp/resample.c")
        if(ANDROID_ABI STREQUAL "x86_64")
            target_compile_definitions(speexdsp PRIVATE USE_SSE)
            message(STATUS "Enabled SSE for SpeexDSP resampler (x86_64)")
        elseif(ANDROID_ABI STREQUAL "arm64-v8a")
            target_compile_definitions(speexdsp PRIVATE USE_NEON)
            message(STATUS "Enabled ARM NEON for SpeexDSP resampler (arm64-v8a)")
        endif()
    endif()


    # Enable SIMD optimizations for audx_src
    if(ANDROID_ABI STREQUAL "x86_64")
        # Android x86_64: explicitly enable SSE4.1
        target_compile_options(audx_src PRIVATE -msse4.1)
        message(STATUS "Enabled SSE4.1 for audx_src (x86_64)")
    endif()

    message(STATUS "Building for Android...")
    target_compile_definitions(audx_src PUBLIC AUDX_ANDROID=1)
endif()

