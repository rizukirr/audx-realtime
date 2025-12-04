# audx-realtime

Lightweight real-time audio denoising library with automatic sample rate conversion. Written in C with SIMD optimizations for high-performance noise suppression.

## What is audx-realtime?

audx-realtime is a minimalist C library to provide real-time speech enhancement with automatic sample rate conversion. It accepts audio at any sample rate (8kHz, 16kHz, 44.1kHz, 48kHz, etc.), automatically resamples to 48kHz for processing, applies neural network-based noise suppression, and resamples back to the original rate.

**Key Features:**
- Simple 3-function API (`create`, `process`, `destroy`)
- Automatic sample rate conversion using SpeexDSP
- SIMD-optimized PCM conversions (SSE4.1/AVX2 for x86, NEON for ARM)
- Voice Activity Detection (VAD) output
- Custom RNNoise model support
- Arena-based memory allocator for fast allocation
- Cross-platform: Linux desktop and Android (windows soon)

## Algorithm

audx-realtime implements the **RNNoise algorithm** - a hybrid DSP/Deep Learning approach developed by Jean-Marc Valin at Xiph.org. The algorithm combines traditional signal processing with recurrent neural networks for effective real-time noise suppression.

### RNNoise Architecture

- **Neural Network**: 3 GRU layers (256 units each) + 2 convolutional layers
- **Frame Size**: 480 samples (10ms at 48kHz)
- **Processing Latency**: ~10-13ms (frame + computation)
- **Model Size**: ~85KB (8-bit quantized weights)
- **Frequency Bands**: 22 for analysis, 32 for gain application
- **Sparse Weights**: 30-50% sparsity for efficiency

### Processing Pipeline

```
Input Audio (any sample rate, 16-bit PCM mono)
    ↓
Resample to 48kHz (if needed)
    ↓
Frame Buffer (480 samples = 10ms)
    ↓
Feature Extraction (42 acoustic features)
    ↓
Neural Network (CNN + GRU layers)
    ↓
Frequency Band Gains + VAD Detection
    ↓
Apply Denoising
    ↓
Resample to original rate (if needed)
    ↓
Denoised Audio Output
```

## Building

### Requirements

- **CMake** 3.23 or higher
- **C99 Compiler**: GCC or Clang
- **Git**: For submodule initialization

### Initialize Submodules

```bash
git submodule update --init --recursive
```

### Desktop (Linux)

```bash
# Debug build (with AddressSanitizer)
./scripts/build.sh debug

# Release build (optimized with -O3 -march=native)
./scripts/build.sh release
```

**Output:**
- Executable: `build/{debug|release}/audx`
- Library: `build/{debug|release}/libaudx_src.so`

### Android

```bash
# Set Android SDK path
export ANDROID_HOME=/path/to/android/sdk

# Debug build (arm64-v8a + x86_64)
./scripts/android.sh debug

# Release build (with LTO and stripped symbols)
./scripts/android.sh release
```

**Output:**
- `libs/arm64-v8a/libaudx_src.so`
- `libs/x86_64/libaudx_src.so`

**Android Requirements:**
- Android NDK 29.0.14206865
- Minimum API level: 24 (Android 7.0)

## Usage

### Library API

```c
#include "audx.h"

// Create denoiser state
// - model_path: Path to custom .rnnn model (or NULL for default)
// - in_rate: Input sample rate (e.g., 16000, 44100, 48000)
// - resample_quality: SpeexDSP quality 0-10 (10 = best, 3-5 recommended)
AudxState *state = audx_create(NULL, 16000, 5);

// Process audio frames
float input[320];   // Frame size for 16kHz: 16000 * 0.01 = 160 samples
float output[320];
float vad_prob = audx_process(state, input, output);

// Or process int16 PCM directly
short pcm_input[320];
short pcm_output[320];
vad_prob = audx_process_int(state, pcm_input, pcm_output);

// Cleanup
audx_destroy(state);
```

### Frame Size Calculation

Each input frame should contain **10ms of audio**:

```c
// Calculate samples per frame for your sample rate
unsigned int frame_size = calculate_frame_sample(sample_rate);

// Examples:
// 8000 Hz  -> 80 samples
// 16000 Hz -> 160 samples
// 44100 Hz -> 441 samples
// 48000 Hz -> 480 samples
```

### Command-Line Tool

```bash
# Process PCM file (auto-detects sample rate from file size)
./build/release/audx input.pcm output.pcm

# Or specify manually
./build/release/audx --rate 16000 input.pcm output.pcm

# Use custom model
./build/release/audx --model my_model.rnnn input.pcm output.pcm
```

**Input Format:**
- **Sample Rate**: Any rate supported by SpeexDSP (e.g., 8000, 16000, 44100, 48000 Hz)
- **Channels**: Mono only (1 channel)
- **Bit Depth**: 16-bit signed PCM
- **Endianness**: Native/little-endian

### Sample Files

Test with included samples:

```bash
# Process airport announcements sample
./build/release/audx \
    samples/noise_test_AirportAnnouncements_1.pcm \
    output_denoised.pcm
```

## Custom Models

You can load custom-trained RNNoise models:

```c
AudxState *state = audx_create("path/to/model.rnnn", 48000, 5);
```

For training custom models, see the [RNNoise repository](https://github.com/xiph/rnnoise) training documentation.

## Performance

### SIMD Optimizations

**x86/x86_64:**
- SSE4.1: int16 ↔ float conversions (8 samples/iteration)
- AVX2: Neural network matrix operations

**ARM/ARM64:**
- NEON: Vectorized conversions (8 samples/iteration)
- Automatic for arm64-v8a

**Fallback:**
- Portable scalar C for unsupported platforms

### Memory Usage

Per `AudxState` instance:
- RNNoise state: ~180KB
- Resampler buffers: ~4-8KB (if resampling needed)
- Arena allocator: Block-based, grows as needed
- **Total**: ~190KB per state

### Latency

- Frame duration: 10ms
- Processing time: 1-3ms (on modern CPUs)
- Resampling overhead: <1ms
- **Total latency**: ~12-14ms

## Integration

### Linking

**CMake:**
```cmake
add_library(audx_src SHARED IMPORTED)
set_target_properties(audx_src PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/libs/${ANDROID_ABI}/libaudx_src.so"
)
target_link_libraries(your_app PRIVATE audx_src)
```

**GCC/Clang:**
```bash
gcc -o myapp myapp.c -laudx_src -L/path/to/libs -I/path/to/include
```

### Android JNI

For Android integration with Kotlin/Java API, see the separate [audx-android](https://github.com/rizukirr/audx-android) project.

## License

MIT License

Copyright (c) 2025 audx-realtime contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Credits

This project builds upon excellent open-source work from the **Xiph.Org Foundation**:

- **RNNoise**: Neural network-based noise suppression by Jean-Marc Valin
  - Copyright (c) 2017-2018, Mozilla
  - https://github.com/xiph/rnnoise

- **SpeexDSP**: High-quality audio resampler
  - Copyright (c) 2002-2008, Xiph.Org Foundation
  - https://github.com/xiph/speexdsp

We gratefully acknowledge the Xiph.Org Foundation and contributors for their foundational work in open-source audio processing.

## References

**RNNoise Algorithm Paper:**

> J.-M. Valin, _A Hybrid DSP/Deep Learning Approach to Real-Time Full-Band Speech Enhancement_
> Proceedings of IEEE Multimedia Signal Processing (MMSP) Workshop, arXiv:1709.08243, 2018.
> https://arxiv.org/pdf/1709.08243.pdf

**Related Links:**

- RNNoise Repository: https://github.com/xiph/rnnoise
- RNNoise Demo: https://jmvalin.ca/demo/rnnoise/
- SpeexDSP Repository: https://github.com/xiph/speexdsp
- Xiph.Org Foundation: https://www.xiph.org/
