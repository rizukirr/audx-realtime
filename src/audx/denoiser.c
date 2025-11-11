#include "denoiser.h"
#include "logger.h"
#include <fcntl.h>
#include <rnnoise.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define PCM_SCALE_FLOAT_MAX 32767.0f
#define PCM_SCALE_FLOAT_MIN -32768.0f

// SIMD intrinsics for different architectures
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||             \
    defined(_M_IX86)
#include <emmintrin.h> // SSE2
#include <smmintrin.h> // SSE4.1 for _mm_cvtepi16_epi32
#define HAS_X86_SIMD 1
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#define HAS_ARM_NEON 1
#endif

/* --- Utility Converters --- */
#ifdef HAS_X86_SIMD
// SSE4.1-optimized int16 to float conversion
static inline void pcm_int16_to_float(const int16_t *input, float *output,
                                      int count) {
  int i = 0;
  // Process 8 samples at a time using SSE4.1
  for (; i <= count - 8; i += 8) {
    __m128i in16 = _mm_loadu_si128((__m128i *)&input[i]);
    // Unpack to 32-bit integers (lower 4 samples)
    __m128i lo32 = _mm_cvtepi16_epi32(in16);
    // Unpack to 32-bit integers (upper 4 samples)
    __m128i hi32 = _mm_cvtepi16_epi32(_mm_srli_si128(in16, 8));
    // Convert to float
    __m128 flo = _mm_cvtepi32_ps(lo32);
    __m128 fhi = _mm_cvtepi32_ps(hi32);
    // Store results
    _mm_storeu_ps(&output[i], flo);
    _mm_storeu_ps(&output[i + 4], fhi);
  }
  // Handle remaining samples (scalar fallback)
  for (; i < count; i++)
    output[i] = (float)input[i];
}

// SSE2-optimized float to int16 conversion with clamping
static inline void pcm_float_to_int16(const float *input, int16_t *output,
                                      int count) {
  int i = 0;
  const __m128 max_val = _mm_set1_ps(PCM_SCALE_FLOAT_MAX);
  const __m128 min_val = _mm_set1_ps(PCM_SCALE_FLOAT_MIN);

  // Process 8 samples at a time using SSE2
  for (; i <= count - 8; i += 8) {
    // Load 8 floats
    __m128 flo = _mm_loadu_ps(&input[i]);
    __m128 fhi = _mm_loadu_ps(&input[i + 4]);
    // Clamp to valid range
    flo = _mm_min_ps(_mm_max_ps(flo, min_val), max_val);
    fhi = _mm_min_ps(_mm_max_ps(fhi, min_val), max_val);
    // Convert to 32-bit integers
    __m128i lo32 = _mm_cvtps_epi32(flo);
    __m128i hi32 = _mm_cvtps_epi32(fhi);
    // Pack to 16-bit integers
    __m128i packed = _mm_packs_epi32(lo32, hi32);
    // Store results
    _mm_storeu_si128((__m128i *)&output[i], packed);
  }
  // Handle remaining samples (scalar fallback)
  for (; i < count; i++) {
    float val = input[i];
    if (val > PCM_SCALE_FLOAT_MAX)
      val = PCM_SCALE_FLOAT_MAX;
    if (val < PCM_SCALE_FLOAT_MIN)
      val = PCM_SCALE_FLOAT_MIN;
    output[i] = (int16_t)val;
  }
}

#elif defined(HAS_ARM_NEON)
// ARM NEON-optimized int16 to float conversion
static inline void pcm_int16_to_float(const int16_t *input, float *output,
                                      int count) {
  int i = 0;
  // Process 8 samples at a time using NEON
  for (; i <= count - 8; i += 8) {
    int16x8_t in16 = vld1q_s16(&input[i]);
    // Widen to 32-bit integers
    int32x4_t lo32 = vmovl_s16(vget_low_s16(in16));
    int32x4_t hi32 = vmovl_s16(vget_high_s16(in16));
    // Convert to float
    float32x4_t flo = vcvtq_f32_s32(lo32);
    float32x4_t fhi = vcvtq_f32_s32(hi32);
    // Store results
    vst1q_f32(&output[i], flo);
    vst1q_f32(&output[i + 4], fhi);
  }
  // Handle remaining samples (scalar fallback)
  for (; i < count; i++)
    output[i] = (float)input[i];
}

// ARM NEON-optimized float to int16 conversion with clamping
static inline void pcm_float_to_int16(const float *input, int16_t *output,
                                      int count) {
  int i = 0;
  const float32x4_t max_val = vdupq_n_f32(PCM_SCALE_FLOAT_MAX);
  const float32x4_t min_val = vdupq_n_f32(PCM_SCALE_FLOAT_MIN);

  // Process 8 samples at a time using NEON
  for (; i <= count - 8; i += 8) {
    // Load 8 floats
    float32x4_t flo = vld1q_f32(&input[i]);
    float32x4_t fhi = vld1q_f32(&input[i + 4]);
    // Clamp to valid range
    flo = vminq_f32(vmaxq_f32(flo, min_val), max_val);
    fhi = vminq_f32(vmaxq_f32(fhi, min_val), max_val);
    // Convert to 32-bit integers (ARMv7-compatible)
    int32x4_t lo32 = vcvtq_s32_f32(flo);
    int32x4_t hi32 = vcvtq_s32_f32(fhi);
    // Narrow to 16-bit integers
    int16x4_t lo16 = vmovn_s32(lo32);
    int16x4_t hi16 = vmovn_s32(hi32);
    // Combine and store
    int16x8_t result = vcombine_s16(lo16, hi16);
    vst1q_s16(&output[i], result);
  }
  // Handle remaining samples (scalar fallback)
  for (; i < count; i++) {
    float val = input[i];
    if (val > PCM_SCALE_FLOAT_MAX)
      val = PCM_SCALE_FLOAT_MAX;
    if (val < PCM_SCALE_FLOAT_MIN)
      val = PCM_SCALE_FLOAT_MIN;
    output[i] = (int16_t)val;
  }
}

#else
// Scalar fallback for platforms without SIMD
static inline void pcm_int16_to_float(const int16_t *input, float *output,
                                      int count) {
  for (int i = 0; i < count; i++)
    output[i] = (float)input[i];
}

static inline void pcm_float_to_int16(const float *input, int16_t *output,
                                      int count) {
  for (int i = 0; i < count; i++) {
    float val = input[i];
    if (val > PCM_SCALE_FLOAT_MAX)
      val = PCM_SCALE_FLOAT_MAX;
    if (val < PCM_SCALE_FLOAT_MIN)
      val = PCM_SCALE_FLOAT_MIN;
    output[i] = (int16_t)val;
  }
}
#endif

/* --- Helper: Cleanup on Partial Failures --- */
static void denoiser_cleanup_partial(struct Denoiser *denoiser) {
  // Clean up denoiser state
  if (denoiser->denoiser_state) {
    rnnoise_destroy(denoiser->denoiser_state);
    denoiser->denoiser_state = NULL;
  }

  // Clean up processing buffer
  if (denoiser->processing_buffer) {
    free(denoiser->processing_buffer);
    denoiser->processing_buffer = NULL;
  }

  // Free model if loaded
  if (denoiser->model) {
    rnnoise_model_free(denoiser->model);
    denoiser->model = NULL;
  }
}

/* --- Initialize Denoiser (Simplified for Mono) --- */
int denoiser_create(const struct DenoiserConfig *config,
                    struct Denoiser *denoiser) {
  if (!config || !denoiser) {
    AUDX_LOGE("denoiser_create: Invalid arguments (config=%p, denoiser=%p)",
              (void *)config, (void *)denoiser);
    return REALTIME_DENOISER_ERROR_INVALID;
  }

  memset(denoiser, 0, sizeof(*denoiser));

  denoiser->num_channels = config->num_channels > 0 ? config->num_channels : 1;

  // Only mono is supported in this optimized version
  if (denoiser->num_channels != 1) {
    AUDX_LOGE(
        "Invalid channel count: %d (only mono supported in optimized version)",
        denoiser->num_channels);
    return REALTIME_DENOISER_ERROR_INVALID;
  }

  denoiser->vad_threshold =
      (config->vad_threshold > 0.0f) ? config->vad_threshold : 0.5f;
  denoiser->enable_vad_output = config->enable_vad_output;

  // Initialize VAD score tracking
  denoiser->min_vad_score = 1.0f;
  denoiser->max_vad_score = 0.0f;

  /* Load model based on configuration */
  if (config->model_path == NULL || config->model_preset == MODEL_EMBEDDED) {
    /* Use embedded model (NULL for rnnoise_create) */
    denoiser->model = NULL;
  } else {
    /* Load custom model from file */
    denoiser->model = rnnoise_model_from_filename(config->model_path);
    if (!denoiser->model) {
      snprintf(denoiser->error_buffer, sizeof(denoiser->error_buffer),
               "Failed to load model from file: %s", config->model_path);
      AUDX_LOGE("Failed to load model from: %s", config->model_path);
      return REALTIME_DENOISER_ERROR_MODEL;
    }
  }

  /* Create RNNoise denoiser state */
  denoiser->denoiser_state = rnnoise_create(denoiser->model);
  if (!denoiser->denoiser_state) {
    AUDX_LOGE("Failed to create rnnoise denoiser");
    denoiser_cleanup_partial(denoiser);
    return REALTIME_DENOISER_ERROR_MEMORY;
  }

  /* Allocate single processing buffer for frame conversion */
  denoiser->processing_buffer = (float *)calloc(FRAME_SIZE, sizeof(float));
  if (!denoiser->processing_buffer) {
    AUDX_LOGE("Failed to allocate processing buffer");
    denoiser_cleanup_partial(denoiser);
    return REALTIME_DENOISER_ERROR_MEMORY;
  }

  AUDX_LOGI("Denoiser created successfully: mono optimized");
  return REALTIME_DENOISER_SUCCESS;
}

/* --- Main Real-Time Frame Processing (Optimized) --- */
int denoiser_process(struct Denoiser *denoiser, const int16_t *input_pcm,
                     int16_t *output_pcm, struct DenoiserResult *result) {
  if (!denoiser || !input_pcm || !output_pcm)
    return REALTIME_DENOISER_ERROR_INVALID;

  const int frame_size = FRAME_SIZE;

  /* Process mono audio directly (no threads, no extra copies, no timing
   * overhead) */
  // Convert int16 PCM to float
  pcm_int16_to_float(input_pcm, denoiser->processing_buffer, frame_size);

  // Denoise in-place
  float vad_score = rnnoise_process_frame(denoiser->denoiser_state,
                                          denoiser->processing_buffer,
                                          denoiser->processing_buffer);

  // Convert float back to int16 PCM
  pcm_float_to_int16(denoiser->processing_buffer, output_pcm, frame_size);

  /* Update statistics (no per-frame timing for optimal performance) */
  denoiser->frames_processed++;
  denoiser->total_vad_score += vad_score;

  if (vad_score >= denoiser->vad_threshold) {
    denoiser->speech_frames++;
  }

  // Update min/max VAD scores
  if (vad_score < denoiser->min_vad_score) {
    denoiser->min_vad_score = vad_score;
  }
  if (vad_score > denoiser->max_vad_score) {
    denoiser->max_vad_score = vad_score;
  }

  if (result) {
    if (denoiser->enable_vad_output) {
      result->vad_probability = vad_score;
      result->is_speech = (vad_score >= denoiser->vad_threshold);
      result->samples_processed = FRAME_SIZE;
    } else {
      // VAD disabled: don't populate result fields
      result->vad_probability = 0.0f;
      result->is_speech = false;
      result->samples_processed = 0;
    }
  }

  return REALTIME_DENOISER_SUCCESS;
}

/* --- Clean Up --- */
void denoiser_destroy(struct Denoiser *denoiser) {
  if (!denoiser)
    return;

  denoiser_cleanup_partial(denoiser);
}

/* Implementation: Get Error */
const char *get_denoiser_error(struct Denoiser *denoiser) {
  if (!denoiser || denoiser->error_buffer[0] == '\0') {
    return NULL;
  }
  return denoiser->error_buffer;
}

/* Implementation: Get Stats */
int get_denoiser_stats(struct Denoiser *denoiser, struct DenoiserStats *stats) {
  if (!denoiser || !stats) {
    return -1;
  }
  float avg_vad = (denoiser->frames_processed > 0)
                      ? (denoiser->total_vad_score / denoiser->frames_processed)
                      : 0.0f;
  float speech_percent =
      (denoiser->frames_processed > 0)
          ? (100.0f * denoiser->speech_frames / denoiser->frames_processed)
          : 0.0f;

  double avg_frame_time =
      (denoiser->frames_processed > 0)
          ? (denoiser->total_processing_time / denoiser->frames_processed)
          : 0.0;

  stats->frame_processed = denoiser->frames_processed;
  stats->speech_detected = speech_percent;
  stats->vscores_avg = avg_vad;
  stats->vscores_min = denoiser->min_vad_score;
  stats->vscores_max = denoiser->max_vad_score;
  stats->ptime_total = denoiser->total_processing_time;
  stats->ptime_avg = avg_frame_time;
  stats->ptime_last = denoiser->last_frame_time;

  return 0;
}

/* Implementation: Version */
const char *denoiser_version(void) { return "1.0.0"; }
