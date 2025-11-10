#include "denoiser.h"
#include "logger.h"
#include <pthread.h>
#include <rnnoise.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define PCM_SCALE_FLOAT_MAX 32767.0f
#define PCM_SCALE_FLOAT_MIN -32768.0f

// SIMD intrinsics for different architectures
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <emmintrin.h> // SSE2
#include <smmintrin.h> // SSE4.1 for _mm_cvtepi16_epi32
#define HAS_X86_SIMD 1
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#define HAS_ARM_NEON 1
#endif

/* --- Worker Thread Function --- */
static void *channel_worker_run(void *arg) {
  ChannelWorker *w = (ChannelWorker *)arg;
  while (true) {
    pthread_mutex_lock(&w->mutex);
    while (!w->has_work && !w->stop)
      pthread_cond_wait(&w->cond, &w->mutex);

    if (w->stop) {
      pthread_mutex_unlock(&w->mutex);
      break;
    }

    // Copy input to output buffer while holding mutex (prevents data races)
    memcpy(w->output_buffer, w->input_buffer, FRAME_SIZE * sizeof(float));
    pthread_mutex_unlock(&w->mutex);

    // Denoise one frame (process output_buffer in-place, no shared data access)
    w->vad_result =
        rnnoise_process_frame(w->state, w->output_buffer, w->output_buffer);

    // Signal completion
    pthread_mutex_lock(&w->mutex);
    w->has_work = false;
    pthread_cond_signal(&w->work_done);
    pthread_mutex_unlock(&w->mutex);
  }
  return NULL;
}

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
    // Convert to 32-bit integers (with rounding)
    int32x4_t lo32 = vcvtnq_s32_f32(flo);
    int32x4_t hi32 = vcvtnq_s32_f32(fhi);
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

static inline void deinterleave_stereo(const int16_t *input, float *left,
                                       float *right, int frame_size) {
  for (int i = 0; i < frame_size; i++) {
    left[i] = (float)input[i * 2];
    right[i] = (float)input[i * 2 + 1];
  }
}

static inline void interleave_stereo(const float *left, const float *right,
                                     int16_t *output, int frame_size) {
  for (int i = 0; i < frame_size; i++) {
    float l = left[i];
    float r = right[i];
    if (l > PCM_SCALE_FLOAT_MAX)
      l = PCM_SCALE_FLOAT_MAX;
    if (l < PCM_SCALE_FLOAT_MIN)
      l = PCM_SCALE_FLOAT_MIN;
    if (r > PCM_SCALE_FLOAT_MAX)
      r = PCM_SCALE_FLOAT_MAX;
    if (r < PCM_SCALE_FLOAT_MIN)
      r = PCM_SCALE_FLOAT_MIN;
    output[i * 2] = (int16_t)l;
    output[i * 2 + 1] = (int16_t)r;
  }
}

/* --- Helper: Cleanup on Partial Failures --- */
static void denoiser_cleanup_partial(struct Denoiser *denoiser, int num_workers,
                                     int num_denoisers, int num_buffers) {
  // Clean up worker threads and their resources
  for (int i = 0; i < num_workers; i++) {
    ChannelWorker *w = &denoiser->workers[i];
    if (w->thread) {
      pthread_mutex_lock(&w->mutex);
      w->stop = true;
      pthread_cond_signal(&w->cond);
      pthread_mutex_unlock(&w->mutex);
      pthread_join(w->thread, NULL);
    }
    pthread_mutex_destroy(&w->mutex);
    pthread_cond_destroy(&w->cond);
    pthread_cond_destroy(&w->work_done);
    if (w->input_buffer)
      free(w->input_buffer);
    if (w->output_buffer)
      free(w->output_buffer);
  }

  // Clean up denoisers
  for (int i = 0; i < num_denoisers; i++) {
    if (denoiser->denoisers && denoiser->denoisers[i]) {
      rnnoise_destroy(denoiser->denoisers[i]);
    }
  }

  // Clean up processing buffers
  for (int i = 0; i < num_buffers; i++) {
    if (denoiser->processing_buffers && denoiser->processing_buffers[i]) {
      free(denoiser->processing_buffers[i]);
    }
  }

  // Free model if loaded
  if (denoiser->model) {
    rnnoise_model_free(denoiser->model);
  }

  // Free arrays
  if (denoiser->processing_buffers)
    free(denoiser->processing_buffers);
  if (denoiser->denoisers)
    free(denoiser->denoisers);
  if (denoiser->workers)
    free(denoiser->workers);
}

/* --- Initialize Denoiser with Worker Threads --- */
int denoiser_create(const struct DenoiserConfig *config,
                    struct Denoiser *denoiser) {
  if (!config || !denoiser) {
    AUDX_LOGE("denoiser_create: Invalid arguments (config=%p, denoiser=%p)",
              (void *)config, (void *)denoiser);
    return REALTIME_DENOISER_ERROR_INVALID;
  }

  memset(denoiser, 0, sizeof(*denoiser));

  denoiser->num_channels = config->num_channels > 0 ? config->num_channels : 1;

  // Validate channel count (only mono and stereo supported)
  if (denoiser->num_channels < 1 || denoiser->num_channels > 2) {
    AUDX_LOGE("Invalid channel count: %d (only 1 or 2 supported)",
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

  /* Allocate arrays */
  denoiser->denoisers = calloc(denoiser->num_channels, sizeof(DenoiseState *));
  if (!denoiser->denoisers) {
    AUDX_LOGE("Failed to allocate denoisers array");
    denoiser_cleanup_partial(denoiser, 0, 0, 0);
    return REALTIME_DENOISER_ERROR_MEMORY;
  }

  denoiser->processing_buffers =
      calloc(denoiser->num_channels, sizeof(float *));
  if (!denoiser->processing_buffers) {
    AUDX_LOGE("Failed to allocate processing_buffers array");
    denoiser_cleanup_partial(denoiser, 0, 0, 0);
    return REALTIME_DENOISER_ERROR_MEMORY;
  }

  denoiser->workers = calloc(denoiser->num_channels, sizeof(ChannelWorker));
  if (!denoiser->workers) {
    AUDX_LOGE("Failed to allocate workers array");
    denoiser_cleanup_partial(denoiser, 0, 0, 0);
    return REALTIME_DENOISER_ERROR_MEMORY;
  }

  /* Initialize channels */
  for (int i = 0; i < denoiser->num_channels; i++) {
    // Create RNNoise denoiser state
    denoiser->denoisers[i] = rnnoise_create(denoiser->model);
    if (!denoiser->denoisers[i]) {
      AUDX_LOGE("Failed to create rnnoise denoiser for channel %d", i);
      denoiser_cleanup_partial(denoiser, i, i, i);
      return REALTIME_DENOISER_ERROR_MEMORY;
    }

    // Allocate processing buffer
    denoiser->processing_buffers[i] = calloc(FRAME_SIZE, sizeof(float));
    if (!denoiser->processing_buffers[i]) {
      AUDX_LOGE("Failed to allocate processing buffer for channel %d", i);
      denoiser_cleanup_partial(denoiser, i, i + 1, i);
      return REALTIME_DENOISER_ERROR_MEMORY;
    }

    // Initialize worker
    ChannelWorker *w = &denoiser->workers[i];
    w->state = denoiser->denoisers[i];
    w->has_work = false;
    w->stop = false;
    w->thread = 0;

    // Allocate worker buffers
    w->input_buffer = calloc(FRAME_SIZE, sizeof(float));
    if (!w->input_buffer) {
      AUDX_LOGE("Failed to allocate input buffer for worker %d", i);
      denoiser_cleanup_partial(denoiser, i, i + 1, i + 1);
      return REALTIME_DENOISER_ERROR_MEMORY;
    }

    w->output_buffer = calloc(FRAME_SIZE, sizeof(float));
    if (!w->output_buffer) {
      AUDX_LOGE("Failed to allocate output buffer for worker %d", i);
      denoiser_cleanup_partial(denoiser, i, i + 1, i + 1);
      return REALTIME_DENOISER_ERROR_MEMORY;
    }

    // Initialize pthread primitives
    int ret = pthread_mutex_init(&w->mutex, NULL);
    if (ret != 0) {
      AUDX_LOGE("Failed to init mutex for worker %d: %d", i, ret);
      denoiser_cleanup_partial(denoiser, i, i + 1, i + 1);
      return REALTIME_DENOISER_ERROR_INVALID;
    }

    ret = pthread_cond_init(&w->cond, NULL);
    if (ret != 0) {
      AUDX_LOGE("Failed to init cond for worker %d: %d", i, ret);
      denoiser_cleanup_partial(denoiser, i, i + 1, i + 1);
      return REALTIME_DENOISER_ERROR_INVALID;
    }

    ret = pthread_cond_init(&w->work_done, NULL);
    if (ret != 0) {
      AUDX_LOGE("Failed to init work_done cond for worker %d: %d", i, ret);
      denoiser_cleanup_partial(denoiser, i, i + 1, i + 1);
      return REALTIME_DENOISER_ERROR_INVALID;
    }

    // Create worker thread
    ret = pthread_create(&w->thread, NULL, channel_worker_run, w);
    if (ret != 0) {
      AUDX_LOGE("Failed to create worker thread %d: %d", i, ret);
      w->thread = 0; // Mark as not created
      denoiser_cleanup_partial(denoiser, i, i + 1, i + 1);
      return REALTIME_DENOISER_ERROR_INVALID;
    }
  }

  AUDX_LOGI("Denoiser created successfully: %d channels", denoiser->num_channels);
  return REALTIME_DENOISER_SUCCESS;
}

/* --- Main Real-Time Frame Processing --- */
int denoiser_process(struct Denoiser *denoiser, const int16_t *input_pcm,
                     int16_t *output_pcm, struct DenoiserResult *result) {
  if (!denoiser || !input_pcm || !output_pcm)
    return REALTIME_DENOISER_ERROR_INVALID;

  /* Start timing */
  struct timespec start_time, end_time;
  clock_gettime(CLOCK_MONOTONIC, &start_time);

  const int frame_size = FRAME_SIZE;
  float total_vad = 0.0f;

  if (denoiser->num_channels == 1) {
    // Mono: process directly (no workers needed)
    pcm_int16_to_float(input_pcm, denoiser->processing_buffers[0], frame_size);
    total_vad = rnnoise_process_frame(denoiser->denoisers[0],
                                      denoiser->processing_buffers[0],
                                      denoiser->processing_buffers[0]);
    pcm_float_to_int16(denoiser->processing_buffers[0], output_pcm, frame_size);
  } else {
    // Stereo: use worker threads with safe buffer handling
    deinterleave_stereo(input_pcm, denoiser->processing_buffers[0],
                        denoiser->processing_buffers[1], frame_size);

    // Copy data to worker input buffers and dispatch work
    for (int i = 0; i < denoiser->num_channels; i++) {
      ChannelWorker *w = &denoiser->workers[i];
      pthread_mutex_lock(&w->mutex);
      // Thread-safe copy: copy data while holding mutex
      memcpy(w->input_buffer, denoiser->processing_buffers[i],
             frame_size * sizeof(float));
      w->has_work = true;
      pthread_cond_signal(&w->cond);
      pthread_mutex_unlock(&w->mutex);
    }

    // Wait for both to finish and copy results back
    for (int i = 0; i < denoiser->num_channels; i++) {
      ChannelWorker *w = &denoiser->workers[i];
      pthread_mutex_lock(&w->mutex);
      while (w->has_work) {
        pthread_cond_wait(&w->work_done, &w->mutex);
      }
      // Thread-safe copy: copy results back while holding mutex
      memcpy(denoiser->processing_buffers[i], w->output_buffer,
             frame_size * sizeof(float));
      total_vad += w->vad_result;
      pthread_mutex_unlock(&w->mutex);
    }
    total_vad /= (float)denoiser->num_channels;

    interleave_stereo(denoiser->processing_buffers[0],
                      denoiser->processing_buffers[1], output_pcm, frame_size);
  }

  /* End timing */
  clock_gettime(CLOCK_MONOTONIC, &end_time);
  double frame_time_ms =
      (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
      (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;

  /* Update statistics */
  denoiser->frames_processed++;
  denoiser->total_vad_score += total_vad;
  denoiser->total_processing_time += frame_time_ms;
  denoiser->last_frame_time = frame_time_ms;

  if (total_vad >= denoiser->vad_threshold) {
    denoiser->speech_frames++;
  }

  // Update min/max VAD scores
  if (total_vad < denoiser->min_vad_score) {
    denoiser->min_vad_score = total_vad;
  }
  if (total_vad > denoiser->max_vad_score) {
    denoiser->max_vad_score = total_vad;
  }

  if (result) {
    if (denoiser->enable_vad_output) {
      result->vad_probability = total_vad;
      result->is_speech = (total_vad >= denoiser->vad_threshold);
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

  for (int i = 0; i < denoiser->num_channels; i++) {
    ChannelWorker *w = &denoiser->workers[i];
    pthread_mutex_lock(&w->mutex);
    w->stop = true;
    pthread_cond_signal(&w->cond);
    pthread_mutex_unlock(&w->mutex);
    pthread_join(w->thread, NULL);
    pthread_mutex_destroy(&w->mutex);
    pthread_cond_destroy(&w->cond);
    pthread_cond_destroy(&w->work_done);
  }

  for (int i = 0; i < denoiser->num_channels; i++) {
    rnnoise_destroy(denoiser->denoisers[i]);
    free(denoiser->processing_buffers[i]);
  }

  /* Only free custom model (embedded model is NULL) */
  if (denoiser->model) {
    rnnoise_model_free(denoiser->model);
  }

  free(denoiser->processing_buffers);
  free(denoiser->denoisers);
  free(denoiser->workers);
}

/* Implementation: Get Error */ const char *
get_denoiser_error(struct Denoiser *denoiser) {
  if (!denoiser || denoiser->error_buffer[0] == '\0') {
    return NULL;
  }
  return denoiser->error_buffer;
}

/* Implementation: Get Stats */
const char *get_denoiser_stats(struct Denoiser *denoiser) {
  if (!denoiser) {
    return NULL;
  }
  static char stats_buffer[512];
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
  snprintf(stats_buffer, sizeof(stats_buffer),
           "Real-Time Denoiser Statistics:\n"
           " Frames processed: %llu\n"
           " Speech detected: %.1f%%\n"
           " VAD scores: avg=%.3f, min=%.3f, max=%.3f\n"
           " Processing time: total=%.3fms, avg=%.3fms/frame, last=%.3fms",
           (unsigned long long)denoiser->frames_processed, speech_percent,
           avg_vad, denoiser->min_vad_score, denoiser->max_vad_score,
           denoiser->total_processing_time, avg_frame_time,
           denoiser->last_frame_time);
  return stats_buffer;
}

/* Implementation: Version */
const char *denoiser_version(void) { return "1.0.0"; }
