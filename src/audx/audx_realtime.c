#include "audx/audx_realtime.h"
#include "audx/common.h"
#include "audx/denoiser.h"
#include "audx/resample.h"
#include <bits/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static inline audx_int32_t get_frame_samples(audx_int32_t input_rate) {
  return input_rate * 10 / 1000;
}

static inline audx_int32_t get_num_frames(long file_size,
                                          audx_int32_t frame_samples) {
  return file_size / (frame_samples * sizeof(audx_int16_t));
}

static inline bool file_exist(const char *path) {
  return access(path, F_OK) == 0;
}

static inline bool is_valid_sample_rate(int rate) {
  return rate >= 8000 && rate <= 192000;
}

static inline bool is_valid_vad_threshold(float vad_threshold) {
  return vad_threshold >= 0.0 && vad_threshold <= 1.0;
}

static inline bool is_valid_model_path(const char *path) {
  return path && file_exist(path);
}

static inline bool is_valid_resample_quality(int quality) {
  return quality > 0 && quality <= 10;
}

static void audx_init(struct AudxState *state) {
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

int audx_process_frame(struct AudxState *state, char *input, char *ret_output,
                       struct AudxStats *ret_stats) {
  if (!state || !input || !ret_output)
    return AUDX_ERROR_INVALID;

  struct AudxState st = *state;
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

  struct DenoiserConfig config = {
      .model_preset = st.model_path ? MODEL_CUSTOM : MODEL_EMBEDDED,
      .model_path = st.model_path,
      .vad_threshold = st.vad_threshold,
      .stats_enabled = st.stats_enabled};

  struct Denoiser denoiser;
  int ret = denoiser_create(&config, &denoiser);
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
