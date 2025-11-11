# audx-realtime

Real-time audio denoising library using Recurrent Neural Networks (RNNoise). Provides low-latency noise suppression for 48kHz mono audio written in C with SIMD optimizations.

**Related Projects:**

- [audx-android](https://github.com/rizukirr/audx-android) - Android library wrapper with Kotlin/Java API

## Algorithm

This project implements the RNNoise algorithm, a hybrid DSP/Deep Learning approach to real-time speech enhancement. The algorithm combines traditional digital signal processing with recurrent neural networks to achieve effective noise suppression with minimal computational overhead.

**Reference Paper:**

> J.-M. Valin, _A Hybrid DSP/Deep Learning Approach to Real-Time Full-Band Speech Enhancement_,
> Proceedings of IEEE Multimedia Signal Processing (MMSP) Workshop, arXiv:1709.08243, 2018.
>
> https://arxiv.org/pdf/1709.08243.pdf

### Architecture

The RNNoise neural network consists of:

- **3 Gated Recurrent Unit (GRU) layers**: 256 units each, forming the recurrent backbone
- **2 Convolutional layers**:
  - Conv1: 65 → 128 features, kernel size 3, tanh activation
  - Conv2: 128 → 256 features, kernel size 3, tanh activation
- **Dense output layer**: 1024 dimensions (4×256 concatenated features) → 32 frequency band gains
- **VAD dense layer**: Voice Activity Detection probability output
- **Sparse weight matrices**: 30-50% sparsity in GRU weights for computational efficiency

**Processing Pipeline:**

```
Input Audio (48kHz, 16-bit PCM)
    ↓
Frame Buffer (480 samples = 10ms)
    ↓
Feature Extraction (42 features from 22 frequency bands)
    ↓
Convolutional Layers (65 → 128 → 256)
    ↓
GRU Layers (3 × 256 units with sparse weights)
    ↓
Dense Layer (1024 → 32 frequency band gains)
    ↓
Apply Gains + VAD Detection
    ↓
Denoised Audio Output
```

**Key Characteristics:**

- Frame size: **480 samples** (10ms at 48kHz)
- Input features: **65 dimensions** (42 acoustic features + context)
- Frequency bands: **22** for analysis, **32** for gain application
- Model size: **~85KB** (8-bit quantized weights)
- Latency: **10ms** (single frame) + processing time (~1-3ms on modern CPUs)

## Features

- **Real-time processing**: Optimized for low-latency audio applications
- **SIMD acceleration**: SSE4.1/AVX2 (x86) and NEON (ARM) optimizations
- **Mono audio processing**: Single-channel optimized for minimal overhead
- **Voice Activity Detection (VAD)**: Integrated speech detection with configurable threshold
- **Audio format validation**: Built-in validation with descriptive error messages
- **Custom model support**: Load user-trained models at runtime
- **Cross-platform**: Linux, Android (arm64-v8a, x86_64)

## Build Requirements

### Dependencies

- **CMake** 3.10 or higher
- **C Compiler**: Clang or GCC with C99 support
- **pthreads**: POSIX threads library
- **Math library** (`-lm`)
- **Git**: For submodule initialization

### Android-Specific

- **Android NDK** 27.0+ (29.0.14206865 recommended)
- **ANDROID_HOME** environment variable set
- Minimum API level: 21
- **Note**: For Android integration, see [audx-android](https://github.com/rizukirr/audx-android) project

### Submodules

Initialize required submodules:

```bash
git submodule update --init --recursive
```

This will clone:

- `external/rnnoise`: RNNoise library from xiph/rnnoise
- `external/Unity`: Unity C testing framework

## Building

### Desktop (Linux/macOS)

#### Debug Build

```bash
./scripts/build.sh debug
```

#### Release Build

```bash
./scripts/build.sh release
```

**Output:**

- Executable: `build/{debug|release}/bin/audx_realtime`
- Library: `build/{debug|release}/lib/libaudx_src.{so|a}`

**Compilation Flags:**

- Debug: `-Wall -Wextra -g -Og`
- Release: `-O3 -march=native -DNDEBUG`

### Android

#### Single ABI (arm64-v8a)

```bash
export ANDROID_HOME=/path/to/android/sdk
./scripts/build.sh android
```

**Output:** `build/android/libs/arm64-v8a/libaudx_src.so`

**Note**: Currently only arm64-v8a is built by default. For integration with audx-android, copy the built library to the Android project's `jniLibs/` directory.

**Android Compilation:**

- Release flags: `-O3 -DNDEBUG`
- NEON optimizations: Automatic for arm64-v8a, `-mfpu=neon` for armeabi-v7a
- No `-march=native` to allow cross-compilation

### CMake Options

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_C_COMPILER=clang
```

**Options:**

- `BUILD_SHARED_LIBS`: `ON` (shared library, default) or `OFF` (static)
- `CMAKE_BUILD_TYPE`: `Debug`, `Release`, or `RelWithDebInfo`
- `ANDROID_ABI`: Target ABI for Android builds
- `ANDROID_PLATFORM`: Minimum API level (default: `android-21`)

## Usage

### Command-Line Tool

Process PCM audio files:

```bash
audx_realtime [OPTIONS] <input.pcm> <output.pcm>
```

**Options:**

- `-c, --channels=N`: Number of channels (must be 1 for mono)
- `-m, --model=PATH`: Path to custom RNNoise model
- `-t, --threshold=VAL`: VAD threshold (0.0-1.0, default: 0.5)
- `-v, --vad`: Enable VAD output in results
- `--no-vad`: Disable VAD output
- `-h, --help`: Show help message

**Examples:**

```bash
# Mono audio with default settings
audx_realtime input.pcm output.pcm

# Custom VAD threshold
audx_realtime -t 0.3 input.pcm output.pcm

# Use custom trained model
audx_realtime --model=my_model.rnnn input.pcm output.pcm

# Disable VAD for pure denoising
audx_realtime --no-vad input.pcm output.pcm
```

**Note**: Only mono (single-channel) audio is currently supported. The `-c` option is kept for compatibility but must be 1.

### API Usage

```c
#include "denoiser.h"

// 1. Configure denoiser
struct DenoiserConfig config = {
    .model_preset = MODEL_EMBEDDED, // or MODEL_CUSTOM
    .model_path = NULL,             // for custom models: "path/to/model.rnnn"
    .vad_threshold = 0.5f,          // 0.0-1.0 (speech detection sensitivity)
    .enable_vad_output = true       // enable VAD probability in results
};

// 2. Create denoiser instance
struct Denoiser denoiser;
int ret = denoiser_create(&config, &denoiser);
if (ret != AUDX_SUCCESS) {
    fprintf(stderr, "Error: %s\n", get_denoiser_error(&denoiser));
    return -1;
}

// 3. Process audio frames (480 samples for mono)
int16_t input[480];  // Mono: 480 samples
int16_t output[480]; // Same size as input
struct DenoiserResult result;

ret = denoiser_process(&denoiser, input, output, &result);
if (ret == AUDX_SUCCESS) {
    printf("VAD probability: %.3f\n", result.vad_probability);
    printf("Speech detected: %s\n", result.is_speech ? "yes" : "no");
    printf("Samples processed: %d\n", result.samples_processed);
}

// 4. Get statistics
struct DenoiserStats stats;
ret = get_denoiser_stats(&denoiser, &stats);
if (ret == AUDX_SUCCESS) {
    printf("Frames processed: %d\n", stats.frame_processed);
    printf("Speech detected: %.1f%%\n", stats.speech_detected);
}

// 5. Cleanup
denoiser_destroy(&denoiser);
```

### Audio Format Requirements

- **Sample rate**: 48 kHz / 48000 Hz (required) - `AUDX_SAMPLE_RATE_48KHZ`
- **Channels**: Mono only (1 channel) - `AUDX_CHANNELS_MONO`
- **Bit depth**: 16-bit signed PCM - `AUDX_BIT_DEPTH_16`
- **Frame size**: Exactly 480 samples - `AUDX_FRAME_SIZE`
- **Endianness**: Native/little-endian

**Format Constants:**

The library defines constants for audio format requirements:

```c
AUDX_SAMPLE_RATE_48KHZ  // 48000 Hz
AUDX_CHANNELS_MONO      // 1 channel
AUDX_BIT_DEPTH_16       // 16-bit PCM
AUDX_FRAME_SIZE         // 480 samples
```

These constants are exposed to Kotlin/Java in the audx-android project via JNI, providing a single source of truth for format requirements.

### Error Codes

```c
AUDX_SUCCESS        //  0: Success
AUDX_ERROR_INVALID  // -1: Invalid parameters
AUDX_ERROR_MEMORY   // -2: Memory allocation failed
AUDX_ERROR_MODEL    // -3: Model loading failed
AUDX_ERROR_FORMAT   // -4: Audio format error
```

### Thread Safety

**Important:** `denoiser_process()` must be called from a **single thread only**.

- Statistics fields (`frames_processed`, etc.) are **NOT thread-safe**
- Do **NOT** call `denoiser_process()` concurrently from multiple threads
- Mono processing uses single-threaded design for minimal overhead

For multi-stream processing, create separate `Denoiser` instances per stream.

## Custom Model Training

You can train custom RNNoise models optimized for specific noise environments or voice characteristics. The training process is provided by the upstream RNNoise repository.

### Training Workflow

#### 1. Prepare Training Data

Collect the following audio files (48kHz, 16-bit PCM):

- **Clean speech**: Speech recordings without noise
- **Background noise**: Continuous noise (HVAC, traffic, crowd)
- **Foreground noise**: Transient sounds (keyboard clicks, mouse, breathing)
- **Room Impulse Responses (optional)**: For realistic reverb simulation

**Recommended dataset size**: 10,000-200,000+ sequences (each ~10 seconds)

#### 2. Extract Features

Generate training features by mixing clean speech with noise:

```bash
cd external/rnnoise

# Basic feature extraction
./dump_features speech.pcm background_noise.pcm foreground_noise.pcm features.f32 100000

# With room impulse response augmentation
./dump_features -rir_list rir_list.txt \
    speech.pcm background_noise.pcm foreground_noise.pcm \
    features.f32 100000
```

**Parameters:**

- `speech.pcm`: Clean speech file
- `background_noise.pcm`: Background noise file
- `foreground_noise.pcm`: Foreground noise file
- `features.f32`: Output feature file (binary float32)
- `100000`: Number of training sequences to generate

**RIR format** (`rir_list.txt`):

```
path/to/rir1.pcm
path/to/rir2.pcm
...
```

#### 3. Train the Model

```bash
cd torch

# Train with default settings (75,000 weight updates recommended)
python3 train_rnnoise.py ../features.f32 output_models --epochs 100

# Advanced training with custom sparsification
python3 train_rnnoise.py ../features.f32 output_models \
    --epochs 100 \
    --batch_size 128 \
    --sparsify_start 6000 \
    --sparsify_stop 20000 \
    --sparsify_interval 100
```

**Training Configuration:**

- **GRU sparsity targets**: W_hr=30%, W_hz=20%, W_hn=50%
- **Sparsification**: Applied between updates 6,000-20,000
- **Block size**: [8, 4] for structured sparsity (hardware-friendly)
- **Learning rate**: Adaptive, typically starts at 1e-3
- **Batch size**: 128 (default)

**Output:** Checkpoints in `output_models/` (e.g., `rnnoise_50.pth`, `rnnoise_75.pth`)

#### 4. Convert Model to C Code

For embedding the model in C/C++ applications:

```bash
# Quantize and convert to C arrays
python3 dump_rnnoise_weights.py --quantize output_models/rnnoise_75.pth rnnoise_c

# This generates C files with weight arrays
```

#### 5. Export Binary Blob (Runtime Loading)

For runtime model loading in audx-realtime:

```bash
# Compile the dump_weights_blob tool
cd external/rnnoise
./compile.sh  # or manually: gcc -o dump_weights_blob dump_weights_blob.c

# Export model to binary format
./dump_weights_blob > my_custom_model.rnnn
```

#### 6. Use Custom Model

```c
struct DenoiserConfig config = {
    .num_channels = 1,
    .model_preset = MODEL_CUSTOM,
    .model_path = "my_custom_model.rnnn",  // Your trained model
    .vad_threshold = 0.5f,
    .enable_vad_output = true
};
```

Or via CLI:

```bash
audx_realtime --model=my_custom_model.rnnn input.pcm output.pcm
```

### Training Tips

1. **Dataset diversity**: Include various speakers, accents, and noise types
2. **Data augmentation**: Use RIR files for realistic acoustic environments
3. **Validation split**: Hold out 10-20% of data for validation
4. **Checkpoint selection**: Test multiple checkpoints (50, 75, 100 epochs) to find optimal balance
5. **Sparsity tuning**: Higher sparsity = faster inference, but may reduce quality
6. **Overfitting**: Monitor validation loss; stop if it diverges from training loss

### Reference Documentation

For detailed training documentation, see:

- **RNNoise training guide**: https://github.com/xiph/rnnoise
- **PyTorch training code**: `external/rnnoise/torch/`
- **Feature extraction**: `external/rnnoise/src/dump_features.c`
- **Model conversion**: `external/rnnoise/dump_rnnoise_weights.py`

## Performance & Optimization

### SIMD Optimizations

The library automatically leverages CPU-specific SIMD instructions:

#### x86/x86_64 Platforms

- **SSE4.1**: int16 ↔ float conversions (8 samples per iteration)
- **SSE2**: Clamping and packing operations
- **AVX2 + FMA**: Neural network matrix operations
- Enabled via `-march=native` (desktop) or explicit flags (Android)

#### ARM Platforms

- **NEON**: Vectorized conversions and math operations (8 samples per iteration)
- ARMv7: Requires `-mfpu=neon` flag
- ARM64: NEON available by default

#### Fallback

- Portable scalar C implementation for unsupported platforms

### Processing Architecture

**Mono processing**: Single-threaded, optimized direct processing path

```
Input Audio (480 int16 samples)
    ↓
Convert int16 → float (SIMD optimized)
    ↓
RNNoise inference (GRU + CNN)
    ↓
Convert float → int16 (SIMD optimized)
    ↓
Output Audio (480 int16 samples)
```

**Performance characteristics:**

- No thread synchronization overhead
- Direct memory access
- Minimal latency
- Optimal for real-time applications

### Timing Statistics

The denoiser tracks performance metrics:

```c
const char *stats = get_denoiser_stats(&denoiser);
```

**Output example:**

```
=== Denoiser Statistics ===
Frames processed: 7000
Speech frames: 4235 (60.5%)
VAD scores: min=0.001, max=0.998, avg=0.623
Total processing time: 21.456 ms
Average frame time: 0.003 ms
Last frame time: 0.003 ms
```

**Metrics:**

- Frame processing time (per-frame and cumulative)
- Speech detection rate
- VAD probability distribution
- Total frames processed

### Memory Usage

Mono processing memory allocation:

- **RNNoise state**: ~180KB (GRU states + FFT buffers)
- **Processing buffer**: ~2KB (480 floats)
- **Total**: ~182KB

**No threading overhead**: Single-threaded design eliminates pthread, mutex, and condition variable overhead

## Testing

### Build Tests

```bash
cd build/debug  # or build/release
cmake --build . --target test_denoiser
```

### Run All Tests

```bash
ctest
```

### Run Specific Test

```bash
./bin/test_denoiser
```

### Test Framework

Tests use the Unity C testing framework (`external/Unity/`).

Test files:

- Unit tests: `tests/unit/`
- Mocks: `tests/mocks/`
- CMake helper: `add_audx_test()` function in `tests/CMakeLists.txt`

### Sample Files

Sample audio files are provided in `samples/`:

- `noise_test_AirportAnnouncements_1.pcm` (noisy)
- `clean_test_AirportAnnouncements_1.pcm` (clean reference)

Format: 48kHz, 16-bit PCM, ~70 seconds each

**Test processing:**

```bash
./build/release/bin/audx_realtime \
    samples/noise_test_AirportAnnouncements_1.pcm \
    output_denoised.pcm
```

## Architecture

### Core Components

1. **Denoiser** (`src/audx/denoiser.c`, `include/audx/denoiser.h`)
   - Main API for audio denoising
   - Worker thread management
   - SIMD-optimized format conversions
   - Statistics collection

2. **Model Loader** (`src/audx/model_loader.c`, `include/audx/model_loader.h`)
   - Model preset management
   - Custom model validation
   - Path resolution utilities

3. **Logger** (`include/audx/logger.h`)
   - Debug logging infrastructure
   - Platform-specific output (logcat for Android)

### External Dependencies

- **RNNoise** (`external/rnnoise`): Xiph.org's RNNoise library
  - Neural network inference engine
  - Feature extraction and DSP
  - Built from source with SIMD optimizations

- **Unity** (`external/Unity`): ThrowTheSwitch Unity testing framework
  - C unit testing
  - Test runner generation

## Platform Notes

### Android Integration

The library builds as a native library for Android. For complete Android integration with Kotlin/Java API, use the **[audx-android](https://github.com/rizukirr/audx-android)** project.

```bash
# Build for arm64-v8a
./scripts/build.sh android

# Output location
build/android/libs/arm64-v8a/libaudx_src.so
```

**Using audx-android:**

The audx-android project provides:

- Kotlin/Java API with coroutines support
- JNI bridge implementation
- Audio format validation (constants exposed via JNI)
- Builder pattern for configuration
- Comprehensive examples and tests

See the [audx-android repository](https://github.com/rizukirr/audx-android) for integration instructions.

**Android logging**: Library uses `__android_log_print()` for debug output (visible via `logcat`).

### Desktop Linux

Standard shared library, link with:

```bash
gcc -o myapp myapp.c -laudx_src -lm -lpthread
```

Or in CMakeLists.txt:

```cmake
target_link_libraries(myapp PRIVATE audx_src)
```

## Project Structure

```
audx-realtime/
├── include/audx/          # Public API headers
│   ├── denoiser.h         # Main denoising API
│   ├── model_loader.h     # Model management
│   └── logger.h           # Logging utilities
├── src/
│   ├── audx/              # Library implementation
│   │   ├── denoiser.c     # Core denoising + SIMD
│   │   └── model_loader.c # Model utilities
│   └── main.c             # CLI tool
├── tests/
│   ├── unit/              # Unit tests
│   └── mocks/             # Test mocks
├── external/              # Git submodules
│   ├── rnnoise/           # RNNoise library
│   └── Unity/             # Test framework
├── scripts/               # Build scripts
│   ├── build.sh           # Desktop builds
│   ├── android.sh         # Multi-ABI Android
│   └── deploy-to-android.sh
├── samples/               # Sample PCM files
├── CMakeLists.txt         # Main build config
```

## Contributing

When contributing, please:

1. Follow the existing C99 code style
2. Add unit tests for new functionality
3. Update documentation for API changes
4. Test on multiple platforms (x86, ARM, Android)
5. Verify SIMD optimizations don't break scalar fallback

## License

This project uses RNNoise from Xiph.org. Check `external/rnnoise/COPYING` for RNNoise licensing terms.

## References

- **RNNoise Paper**: https://arxiv.org/pdf/1709.08243.pdf
- **RNNoise Repository**: https://github.com/xiph/rnnoise
- **Xiph.org**: https://www.xiph.org/

## Troubleshooting

### Submodule not initialized

```bash
git submodule update --init --recursive
```

### Android NDK not found

```bash
export ANDROID_HOME=/path/to/android/sdk
# Verify NDK installation
ls $ANDROID_HOME/ndk/
```

### Compilation errors with SIMD

Check CPU architecture:

```bash
gcc -march=native -Q --help=target | grep march
```

Disable SIMD by removing `-march=native` in `CMakeLists.txt`.

### Audio quality issues

- Verify input is 48kHz, 16-bit PCM
- Try different VAD thresholds (`-t` flag)
- Consider training a custom model for your noise environment
- Check frame size is exactly 480 samples per channel

### Performance issues

- Use Release build (`-DCMAKE_BUILD_TYPE=Release`)
- Verify SIMD optimizations are enabled (check compilation flags)
- Profile with `get_denoiser_stats()` to identify bottlenecks
- For real-time applications, use appropriate thread priorities
