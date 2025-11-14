# Gemini Project Overview: audx-realtime

This document provides a comprehensive overview of the `audx-realtime` project, intended to be used as a context for AI-driven development.

## Project Overview

`audx-realtime` is a real-time audio denoising library written in C. It utilizes the RNNoise algorithm, a deep learning-based approach for noise suppression, to provide low-latency denoising for 48kHz mono audio. The library is optimized for performance with SIMD instructions (SSE/AVX for x86 and NEON for ARM) and is designed for cross-platform use, with support for Linux, macOS, and Android.

The project is structured as a CMake project and includes the following key components:

*   **`audx_realtime` library:** The core denoising library.
*   **`audx_realtime` executable:** A command-line tool for processing PCM audio files.
*   **Dependencies:** The project includes `rnnoise` and `speexdsp` as Git submodules, which are built from source.

## Building and Running

The project uses CMake for building and provides shell scripts for convenience.

### Dependencies

*   CMake 3.10+
*   C Compiler (Clang or GCC)
*   Git

### Building

The project can be built in either `debug` or `release` mode.

**Debug Build:**

```bash
./scripts/build.sh debug
```

**Release Build:**

```bash
./scripts/build.sh release
```

The build outputs are placed in the `build/` directory. The executable can be found in `build/{debug|release}/bin/` and the library in `build/{debug|release}/lib/`.

### Running the Command-Line Tool

The command-line tool can be used to process a raw PCM audio file.

```bash
./build/release/bin/audx_realtime [OPTIONS] <input.pcm> <output.pcm>
```

**Example:**

```bash
./build/release/bin/audx_realtime samples/noise_test_AirportAnnouncements_1.pcm output.pcm
```

### Testing

The project uses the Unity testing framework. Tests can be run using CTest.

```bash
cd build/debug
ctest
```

## Development Conventions

*   **Code Style:** The project follows the C99 standard.
*   **API:** The public API is defined in `include/audx/denoiser.h`. The API is well-documented with comments.
*   **Threading:** The `denoiser_process()` function is not thread-safe and should only be called from a single thread. For multi-stream processing, separate `Denoiser` instances should be created for each stream.
*   **Error Handling:** The library uses error codes and provides a `get_denoiser_error()` function to retrieve a human-readable error message.
*   **Dependencies:** Dependencies are managed as Git submodules in the `external/` directory.

*   **External Dependencies:** Code within external dependency directories (e.g., `external/`) must not be modified. Changes should only be made to the project's core codebase.
