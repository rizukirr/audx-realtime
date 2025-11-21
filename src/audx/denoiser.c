#include "audx/denoiser.h"
#include "audx/common.h"
#include "audx/logger.h"
#include "audx/model_loader.h"
#include <fcntl.h>
#include <rnnoise.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    return AUDX_ERROR_INVALID;
  }

  memset(denoiser, 0, sizeof(*denoiser));

  denoiser->num_channels = 1;

  denoiser->vad_threshold =
      (config->vad_threshold > 0.0f) ? config->vad_threshold : 0.5f;
  denoiser->stats_enabled = config->stats_enabled;

  // Initialize VAD score tracking
  denoiser->min_vad_score = 1.0f;
  denoiser->max_vad_score = 0.0f;

  /* Load model based on configuration */
  if (config->model_path == NULL || config->model_preset == MODEL_EMBEDDED) {
    /* Use embedded model (NULL for rnnoise_create) */
    denoiser->model = NULL;
  } else {
    /* Validate model file before attempting to load */
    if (!validate_model_file(config->model_path)) {
      snprintf(denoiser->error_buffer, sizeof(denoiser->error_buffer),
               "Invalid or non-existent model file: %s", config->model_path);
      AUDX_LOGE("Invalid model file: %s", config->model_path);
      return AUDX_ERROR_INVALID;
    }
    /* Load custom model from file */
    denoiser->model = rnnoise_model_from_filename(config->model_path);
    if (!denoiser->model) {
      snprintf(denoiser->error_buffer, sizeof(denoiser->error_buffer),
               "Failed to load model from file: %s", config->model_path);
      AUDX_LOGE("Failed to load model from: %s", config->model_path);
      return AUDX_ERROR_INVALID;
    }
  }

  /* Create RNNoise denoiser state */
  denoiser->denoiser_state = rnnoise_create(denoiser->model);
  if (!denoiser->denoiser_state) {
    AUDX_LOGE("Failed to create rnnoise denoiser");
    denoiser_cleanup_partial(denoiser);
    return AUDX_ERROR_MEMORY;
  }

  /* Allocate single processing buffer for frame conversion */
  denoiser->processing_buffer =
      (float *)calloc(AUDX_DEFAULT_FRAME_SIZE, sizeof(float));
  if (!denoiser->processing_buffer) {
    AUDX_LOGE("Failed to allocate processing buffer");
    denoiser_cleanup_partial(denoiser);
    return AUDX_ERROR_MEMORY;
  }

  AUDX_LOGI("Denoiser created successfully: mono optimized");
  return AUDX_SUCCESS;
}

/* --- Main Real-Time Frame Processing (Optimized) --- */
static inline double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

int denoiser_process(struct Denoiser *denoiser, const int16_t *input_pcm,
                     int16_t *output_pcm, struct DenoiserResult *result) {
  if (!denoiser || !input_pcm || !output_pcm || !result)
    return AUDX_ERROR_INVALID;

  const int frame_size = AUDX_DEFAULT_FRAME_SIZE;

  double t0 = 0.0;
  if (denoiser->stats_enabled) {
    /* start timing */
    t0 = now_ms();
  }

  /* Process mono audio directly (no threads, no extra copies, no timing
   * overhead) */
  // Convert int16 PCM to float
  pcm_int16_to_float(input_pcm, denoiser->processing_buffer, frame_size);

  // Denoise in-place
  float vad_score = rnnoise_process_frame(denoiser->denoiser_state,
                                          denoiser->processing_buffer,
                                          denoiser->processing_buffer);

  if (result) {
    result->vad_probability = vad_score;
    result->is_speech = (vad_score >= denoiser->vad_threshold);
    result->samples_processed = AUDX_DEFAULT_FRAME_SIZE;
  }

  // Convert float back to int16 PCM
  pcm_float_to_int16(denoiser->processing_buffer, output_pcm, frame_size);

  /* Update statistics */
  if (denoiser->stats_enabled) {

    /* end timing */
    double t1 = now_ms();
    double elapsed_ms = t1 - t0; /* elapsed in milliseconds */

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

    denoiser->last_frame_time = (float)elapsed_ms;
    denoiser->total_processing_time += (float)elapsed_ms;
  }

  return AUDX_SUCCESS;
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

int get_denoiser_stats_reset(struct DenoiserStats *stats) {
  if (!stats) {
    return AUDX_ERROR_INVALID;
  }
  memset(stats, 0, sizeof(*stats));
  return AUDX_SUCCESS;
}

/* Implementation: Version */
const char *denoiser_version(void) { return "1.0.0"; }
