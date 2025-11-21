#include "audx/common.h"
#include "audx/denoiser.h"
#include "audx/resample.h"
#include <fcntl.h>
#include <getopt.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Default configuration values */

static void print_usage(const char *program_name) {
  printf("Usage: %s [OPTIONS] <input.pcm> <output.pcm>\n", program_name);
  printf(
      "\nReal-Time Audio Denoiser - Processes 48kHz 16-bit PCM audio through "
      "RNNoise\n");
  printf("\nRequired arguments:\n");
  printf("  <input.pcm>          Input raw PCM file\n");
  printf("  <output.pcm>         Output denoised PCM file\n");
  printf("\nOptional parameters:\n");
  printf("  -c, --channels=N     Number of channels: 1 (mono) or 2 (stereo)\n");
  printf("                       Default: %d\n", AUDX_DEFAULT_CHANNELS);
  printf("  -r, --rate=RATE      Input sample rate if not 48kHz\n");
  printf("                       Audio will be resampled to 48kHz for "
         "denoising\n");
  printf("  -q, --resample-quality=N  Resampling quality (0-10)\n");
  printf("                       0=fastest, 10=best quality\n");
  printf("                       Default: %d\n", AUDX_DEFAULT_RESAMPLE_QUALITY);
  printf("  -m, --model=PATH     Path to custom RNNoise model file\n");
  printf("                       Default: embedded model\n");
  printf(
      "  -t, --threshold=VAL  VAD threshold (0.0-1.0 for speech detection)\n");
  printf("                       Default: %.1f\n", AUDX_DEFAULT_VAD_THRESHOLD);
  printf("  -s, --stats          Enable statistics output\n");
  printf("      --no-stats       Disable statistics output\n");
  printf("                       Default: %s\n",
         AUDX_DEFAULT_STATS_ENABLED ? "enabled" : "disabled");
  printf("  -h, --help           Show this help message\n");
  printf("\nExamples:\n");
  printf("  %s input.pcm output.pcm\n", program_name);
  printf("  %s -c 2 -t 0.3 input.pcm output.pcm\n", program_name);
  printf("  %s --model=custom.rnnn input.pcm output.pcm\n", program_name);
  printf("  %s -c 1 -t 0.7 --no-stats input.pcm output.pcm\n", program_name);
  printf("  %s -r 24000 -q 5 input.pcm output.pcm\n", program_name);
}

/**
 * @brief AudX runtime configuration state.
 *
 * Holds all configurable parameters required for processing audio.
 * Values are validated and normalized via audx_init().
 */
struct AudxConfig {
  /**
   * Input audio sample rate in Hz.
   * Must be within 8000–192000 Hz.
   */
  audx_int32_t input_rate;

  /**
   * Resampling quality level (1–10).
   * Higher values increase CPU usage but improve accuracy.
   */
  audx_uint32_t resample_quality;

  /**
   * Path to the denoiser or VAD model file.
   * Must be a valid existing file or NULL.
   */
  char *model_path;

  /**
   * Voice Activity Detection (VAD) threshold.
   * Accepted range is 0.0–1.0.
   */
  float vad_threshold;

  /**
   * Enables or disables statistics collection.
   * If false, AudxStats fields may not be updated.
   */
  bool stats_enabled;
};

/**
 * @brief Callback function type for progress reporting.
 *
 * Called periodically (0–100%) during audio processing to indicate progress.
 *
 * @param progress  Current progress percentage.
 */
typedef void (*AudxProgressCallback)(int progress);

/**
 * @brief Runtime statistics collected during audio processing.
 *
 * Updated per-frame depending on the stats_enabled flag in AudxConfig.
 * All time values are in milliseconds.
 */
struct AudxStats {
  /** Number of samples in one input frame (10 ms). */
  audx_int32_t input_frame_samples;

  /** Total number of frames expected for the input file. */
  audx_int32_t num_frames;

  /** Total size of the input audio file in bytes. */
  long file_size;

  /**
   * Optional callback invoked during processing to report percentage progress.
   */
  AudxProgressCallback on_progress;

  /** Total number of frames processed since initialization. */
  int frame_processed;

  /** Percentage of frames containing detected speech (0.0–100.0). */
  float speech_detected;

  /** Average VAD score across all processed frames. */
  float vscores_avg;

  /** Minimum VAD score observed across frames. */
  float vscores_min;

  /** Maximum VAD score observed across frames. */
  float vscores_max;

  /** Total accumulated processing time across all frames (ms). */
  float ptime_total;

  /** Average processing time per frame (ms). */
  float ptime_avg;

  /** Processing time for the most recent frame (ms). */
  float ptime_last;
};

/**
 * @brief Calculate the total number of audio frames in a PCM file.
 *
 * Divides the total file size by the number of bytes per frame.
 *
 * @param file_size      Size of the PCM file in bytes.
 * @param frame_samples  Number of samples per frame.
 * @return Total number of frames in the file.
 */
static inline audx_int32_t get_num_frames(long file_size,
                                          audx_int32_t frame_samples) {
  return file_size / (frame_samples * sizeof(audx_int16_t));
}

/**
 * @brief Check whether a file exists on the filesystem.
 *
 * Uses access() to test for file presence.
 *
 * @param path  Path to the file.
 * @return true if the file exists, false otherwise.
 */
static inline bool file_exist(const char *path) {
  return access(path, F_OK) == 0;
}

/**
 * @brief Validate an audio sample rate.
 *
 * Accepts sample rates between 8000 Hz and 192000 Hz.
 *
 * @param rate  Sample rate to validate.
 * @return true if valid, false otherwise.
 */
static inline bool is_valid_sample_rate(int rate) {
  return rate >= 8000 && rate <= 192000;
}

/**
 * @brief Validate VAD threshold.
 *
 * Threshold must be between 0.0 and 1.0.
 *
 * @param vad_threshold  VAD threshold value.
 * @return true if valid, false otherwise.
 */
static inline bool is_valid_vad_threshold(float vad_threshold) {
  return vad_threshold >= 0.0 && vad_threshold <= 1.0;
}

/**
 * @brief Check whether model path is valid.
 *
 * Validates that the path is non-null and the file exists.
 *
 * @param path  Path to the model file.
 * @return true if valid, false otherwise.
 */
static inline bool is_valid_model_path(const char *path) {
  return path && file_exist(path);
}

/**
 * @brief Validate resampling quality value.
 *
 * Allowed range: 1–10.
 *
 * @param quality  Resampling quality level.
 * @return true if valid, false otherwise.
 */
static inline bool is_valid_resample_quality(int quality) {
  return quality > 0 && quality <= 10;
}

/**
 * @brief Initialize AudxConfig with validated defaults.
 *
 * Ensures all configuration values are valid; replaces invalid values
 * with library defaults.
 *
 * @param state  Pointer to AudxConfig to initialize.
 */
static void audx_init(struct AudxConfig *state) {
  if (!is_valid_model_path(state->model_path)) {
    state->model_path = NULL;
  }

  if (!is_valid_vad_threshold(state->vad_threshold)) {
    state->vad_threshold = AUDX_DEFAULT_VAD_THRESHOLD;
  }

  if (!is_valid_sample_rate(state->input_rate)) {
    state->input_rate = AUDX_DEFAULT_SAMPLE_RATE;
  }

  if (!is_valid_resample_quality(state->resample_quality)) {
    state->resample_quality = AUDX_DEFAULT_RESAMPLE_QUALITY;
  }
}

int audx_process_frame(struct AudxConfig *config, char *input, char *ret_output,
                       struct AudxStats *ret_stats) {
  if (!config || !input || !ret_output)
    return AUDX_ERROR_INVALID;

  struct AudxConfig st = *config;
  audx_init(&st);

  audx_int16_t *input_buffer = NULL;
  audx_int16_t *output_buffer = NULL;
  audx_int16_t *resampled_input = NULL;
  audx_int16_t *resampled_output = NULL;
  AudxResampler upsampler = NULL;
  AudxResampler downsampler = NULL;

  FILE *input_file = fopen(input, "rb");
  if (!input_file) {
    fprintf(stderr, "Error: Cannot open input file '%s'\n", input);
    return AUDX_ERROR_INVALID;
  }

  /* Get file size */
  fseek(input_file, 0, SEEK_END);
  long file_size = ftell(input_file);
  fseek(input_file, 0, SEEK_SET);

  bool needs_resampling = (st.input_rate != AUDX_DEFAULT_SAMPLE_RATE);
  audx_int32_t input_frame_samples = get_frame_samples(st.input_rate);
  audx_int32_t denoiser_frame_samples = AUDX_DEFAULT_FRAME_SIZE; // always 48kHz
  audx_int32_t num_frames = get_num_frames(file_size, input_frame_samples);

  if (st.stats_enabled) {
    ret_stats->input_frame_samples = input_frame_samples;
    ret_stats->num_frames = num_frames;
    ret_stats->file_size = file_size;
  }

  struct DenoiserConfig denoiser_config = {
      .model_preset = st.model_path ? MODEL_CUSTOM : MODEL_EMBEDDED,
      .model_path = st.model_path,
      .vad_threshold = st.vad_threshold,
      .stats_enabled = st.stats_enabled};

  struct Denoiser denoiser;
  int ret = denoiser_create(&denoiser_config, &denoiser);
  if (ret != AUDX_SUCCESS) {
    fprintf(stderr, "Error: Failed to create denoiser (code=%d)\n", ret);
    const char *error = get_denoiser_error(&denoiser);
    if (error) {
      fprintf(stderr, " %s\n", error);
    }
    fclose(input_file);
    return ret;
  }

  FILE *output_file = fopen(ret_output, "wb");
  if (!output_file) {
    fprintf(stderr, "Error: Cannot create output file '%s'\n", ret_output);
    ret = AUDX_ERROR_INVALID;
    goto cleanup;
  }

  input_buffer = malloc(input_frame_samples * sizeof(audx_int16_t));
  output_buffer = malloc(input_frame_samples * sizeof(audx_int16_t));

  if (!input_buffer || !output_buffer) {
    fprintf(stderr,
            "Error: Memory allocation failed for input/output buffer\n");
    ret = AUDX_ERROR_MEMORY;
    goto cleanup;
  }

  if (needs_resampling) {
    resampled_input = malloc(denoiser_frame_samples * sizeof(audx_int16_t));
    resampled_output = malloc(denoiser_frame_samples * sizeof(audx_int16_t));

    if (!resampled_input || !resampled_output) {
      fprintf(stderr, "Error: Memory allocation failed for resample buffers\n");
      ret = AUDX_ERROR_MEMORY;
      goto cleanup;
    }

    int err;
    upsampler = audx_resample_create(AUDX_DEFAULT_CHANNELS, st.input_rate,
                                     AUDX_DEFAULT_SAMPLE_RATE,
                                     st.resample_quality, &err);
    downsampler =
        audx_resample_create(AUDX_DEFAULT_CHANNELS, AUDX_DEFAULT_SAMPLE_RATE,
                             st.input_rate, st.resample_quality, &err);

    if (!upsampler || !downsampler) {
      fprintf(stderr, "Error: Failed to create resamplers\n");
      ret = AUDX_ERROR_INVALID;
      goto cleanup;
    }
  }

  struct timespec start_time, end_time;
  if (st.stats_enabled) {
    clock_gettime(CLOCK_MONOTONIC, &start_time);
  }

  audx_int32_t frame_count = 0;

  while (true) {
    size_t read = fread(input_buffer, sizeof(audx_int16_t), input_frame_samples,
                        input_file);
    if (read == 0) {
      break; /* End of file */
    }

    if (read < (size_t)input_frame_samples) {
      memset(input_buffer + read, 0,
             (input_frame_samples - read) * sizeof(audx_int16_t));
    }

    int16_t *process_input = input_buffer;
    int16_t *process_output =
        needs_resampling ? resampled_output : output_buffer;

    if (needs_resampling) {
      audx_uint32_t in_len = input_frame_samples;
      audx_uint32_t out_len = denoiser_frame_samples;
      ret = audx_resample_process(upsampler, input_buffer, &in_len,
                                  resampled_input, &out_len);
      if (ret != AUDX_SUCCESS) {
        fprintf(stderr, "Error resampling input frame %d (code=%d)\n",
                frame_count, ret);
        break;
      }
      process_input = resampled_input;
    }

    struct DenoiserResult result;
    ret = denoiser_process(&denoiser, process_input, process_output, &result);
    if (ret != AUDX_SUCCESS) {
      fprintf(stderr, "Error processing frame %d\n", frame_count);
      break;
    }

    if (needs_resampling) {
      audx_uint32_t in_len = denoiser_frame_samples;
      audx_uint32_t out_len = input_frame_samples;
      ret = audx_resample_process(downsampler, process_output, &in_len,
                                  output_buffer, &out_len);
      if (ret != AUDX_SUCCESS) {
        fprintf(stderr, "Error resampling output frame %d (code=%d)\n",
                frame_count, ret);
        break;
      }
    }

    size_t samples_to_write = (read < (size_t)input_frame_samples)
                                  ? read
                                  : (size_t)input_frame_samples;
    fwrite(output_buffer, sizeof(int16_t), samples_to_write, output_file);

    frame_count++;
    if (frame_count % 100 == 0 || read < (size_t)input_frame_samples) {
      ret_stats->on_progress(frame_count);
    }
  }

  if (st.stats_enabled) {
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double total_time_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                           (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;
    double avg_time_ms = frame_count > 0 ? total_time_ms / frame_count : 0.0;

    denoiser.total_processing_time = total_time_ms;
    denoiser.last_frame_time = avg_time_ms;
  }

  struct DenoiserStats denoiser_stats;
  ret = get_denoiser_stats(&denoiser, &denoiser_stats);
  if (ret < 0) {
    fprintf(stderr, "Error: Failed to get statistics\n");
    goto cleanup;
  }

  if (st.stats_enabled) {
    ret_stats->frame_processed = denoiser_stats.frame_processed;
    ret_stats->speech_detected = denoiser_stats.speech_detected;
    ret_stats->vscores_avg = denoiser_stats.vscores_avg;
    ret_stats->vscores_min = denoiser_stats.vscores_min;
    ret_stats->vscores_max = denoiser_stats.vscores_max;
    ret_stats->ptime_total = denoiser_stats.ptime_total;
    ret_stats->ptime_avg = denoiser_stats.ptime_avg;
    ret_stats->ptime_last = denoiser_stats.ptime_last;

    get_denoiser_stats_reset(&denoiser_stats);
  }

  ret = AUDX_SUCCESS;
  goto cleanup;

cleanup:
  audx_resample_destroy(upsampler);
  audx_resample_destroy(downsampler);

  if (input_buffer) {
    free(input_buffer);
  }
  if (output_buffer) {
    free(output_buffer);
  }
  if (resampled_input) {
    free(resampled_input);
  }
  if (resampled_output) {
    free(resampled_output);
  }

  denoiser_destroy(&denoiser);

  if (input_file) {
    fclose(input_file);
  }
  if (output_file) {
    fclose(output_file);
  }
  return ret;
}

void print_progress(int progress) {
  printf("\rProcessing frame: %d", progress);
  fflush(stdout);
}

int main(int argc, char *argv[]) {
  /* Default configuration */
  int channels = AUDX_DEFAULT_CHANNELS;
  const char *model_path = NULL;
  float vad_threshold = AUDX_DEFAULT_VAD_THRESHOLD;
  bool stats_enabled = AUDX_DEFAULT_STATS_ENABLED;
  int input_rate = AUDX_DEFAULT_SAMPLE_RATE;
  int resample_quality = AUDX_DEFAULT_RESAMPLE_QUALITY;

  /* Long options */
  static struct option long_options[] = {
      {"channels", required_argument, 0, 'c'},
      {"rate", required_argument, 0, 'r'},
      {"resample-quality", required_argument, 0, 'q'},
      {"model", required_argument, 0, 'm'},
      {"threshold", required_argument, 0, 't'},
      {"stats", no_argument, 0, 's'},
      {"no-stats", no_argument, 0, 1},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

  /* Parse options */
  int opt;
  while ((opt = getopt_long(argc, argv, "c:r:q:m:t:sh", long_options, NULL)) !=
         -1) {
    switch (opt) {
    case 'c':
      channels = atoi(optarg);
      if (channels != 1) {
        fprintf(stderr, "Error: channels must be 1 (mono)\n");
        return 1;
      }
      break;
    case 'r':
      input_rate = atoi(optarg);
      if (input_rate <= 0) {
        fprintf(stderr, "Error: Invalid sample rate\n");
        return 1;
      }
      break;
    case 'q':
      if (input_rate == AUDX_DEFAULT_SAMPLE_RATE) {
        fprintf(stderr,
                "Warning: Resample quality is work with --rate/r argument\n");
      }
      resample_quality = atoi(optarg);
      if (resample_quality < AUDX_RESAMPLER_QUALITY_MIN ||
          resample_quality > AUDX_RESAMPLER_QUALITY_MAX) {
        fprintf(stderr, "Error: Resample quality must be between %d and %d\n",
                AUDX_RESAMPLER_QUALITY_MIN, AUDX_RESAMPLER_QUALITY_MAX);
        return 1;
      }
      break;
    case 'm':
      model_path = optarg;
      break;
    case 't':
      vad_threshold = atof(optarg);
      if (vad_threshold < 0.0f || vad_threshold > 1.0f) {
        fprintf(stderr, "Error: VAD threshold must be between 0.0 and 1.0\n");
        return 1;
      }
      break;
    case 's':
      stats_enabled = true;
      break;
    case 1:
      stats_enabled = false;
      break;
    case 'h':
      print_usage(argv[0]);
      return 0;
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  /* Check for required positional arguments */
  if (optind + 2 > argc) {
    fprintf(stderr, "Error: Missing required arguments\n\n");
    print_usage(argv[0]);
    return 1;
  }

  const char *input_path = argv[optind];
  const char *output_path = argv[optind + 1];

  printf("Real-Time Audio Denoiser v%s\n", denoiser_version());
  printf("Input:         %s\n", input_path);
  printf("Output:        %s\n", output_path);
  printf("Channels:      %d\n", channels);
  printf("Input Rate:    %d Hz\n", input_rate);
  if (input_rate != AUDX_DEFAULT_SAMPLE_RATE) {
    printf("Output Rate:   %d Hz (resampled back from 48kHz)\n", input_rate);
    printf("Resample Quality: %d\n", resample_quality);
  }
  printf("Model:         %s\n", model_path ? model_path : "embedded");
  printf("VAD Threshold: %.2f\n", vad_threshold);
  printf("Statistics:    %s\n", stats_enabled ? "enabled" : "disabled");

  struct AudxConfig st = {
      .input_rate = input_rate,
      .resample_quality = resample_quality,
      .model_path = (char *)model_path,
      .vad_threshold = vad_threshold,
      .stats_enabled = stats_enabled,
  };

  struct AudxStats stats = {
      .on_progress = print_progress,
  };

  int ret =
      audx_process_frame(&st, (char *)input_path, (char *)output_path, &stats);
  if (ret != AUDX_SUCCESS) {
    fprintf(stderr, "Error: Denoising failed (code=%d)\n", ret);
    return 1;
  }

  if (stats_enabled) {
    char stats_buffer[1024];
    snprintf(stats_buffer, sizeof(stats_buffer),
             "Real-Time Denoiser Statistics:\n"
             " Frames processed: %d\n"
             " Speech detected: %.1f%%\n"
             " VAD scores: avg=%.3f, min=%.3f, max=%.3f\n"
             " Processing time: total=%.3fms, avg=%.3fms/frame, last=%.3fms",
             stats.frame_processed, stats.speech_detected, stats.vscores_avg,
             stats.vscores_min, stats.vscores_max, stats.ptime_total,
             stats.ptime_avg, stats.ptime_last);

    printf("\n%s\n", stats_buffer);
  }

  printf("\nOutput written to: %s\n", output_path);
  return 0;
}
