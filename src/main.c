#include "denoiser.h"
#include "model_loader.h"
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Default configuration values */
#define DEFAULT_CHANNELS 1
#define DEFAULT_VAD_THRESHOLD 0.5f
#define DEFAULT_ENABLE_VAD true

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
  printf("                       Default: %d\n", DEFAULT_CHANNELS);
  printf("  -m, --model=PATH     Path to custom RNNoise model file\n");
  printf("                       Default: embedded model\n");
  printf(
      "  -t, --threshold=VAL  VAD threshold (0.0-1.0 for speech detection)\n");
  printf("                       Default: %.1f\n", DEFAULT_VAD_THRESHOLD);
  printf("  -v, --vad            Enable VAD output in results\n");
  printf("      --no-vad         Disable VAD output\n");
  printf("                       Default: %s\n",
         DEFAULT_ENABLE_VAD ? "enabled" : "disabled");
  printf("  -h, --help           Show this help message\n");
  printf("\nExamples:\n");
  printf("  %s input.pcm output.pcm\n", program_name);
  printf("  %s -c 2 -t 0.3 input.pcm output.pcm\n", program_name);
  printf("  %s --model=custom.rnnn input.pcm output.pcm\n", program_name);
  printf("  %s -c 1 -t 0.7 --no-vad input.pcm output.pcm\n", program_name);
}

int main(int argc, char *argv[]) {
  /* Default configuration */
  int channels = DEFAULT_CHANNELS;
  const char *model_path = NULL;
  float vad_threshold = DEFAULT_VAD_THRESHOLD;
  bool enable_vad = DEFAULT_ENABLE_VAD;

  /* Long options */
  static struct option long_options[] = {
      {"channels", required_argument, 0, 'c'},
      {"model", required_argument, 0, 'm'},
      {"threshold", required_argument, 0, 't'},
      {"vad", no_argument, 0, 'v'},
      {"no-vad", no_argument, 0, 'V'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

  /* Parse options */
  int opt;
  while ((opt = getopt_long(argc, argv, "c:m:t:vh", long_options, NULL)) !=
         -1) {
    switch (opt) {
    case 'c':
      channels = atoi(optarg);
      if (channels < 1 || channels > 2) {
        fprintf(stderr, "Error: channels must be 1 or 2\n");
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
    case 'v':
      enable_vad = true;
      break;
    case 'V':
      enable_vad = false;
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
  printf("Input:        %s\n", input_path);
  printf("Output:       %s\n", output_path);
  printf("Channels:     %d\n", channels);
  printf("Model:        %s\n", model_path ? model_path : "embedded");
  printf("VAD Threshold: %.2f\n", vad_threshold);
  printf("VAD Output:   %s\n", enable_vad ? "enabled" : "disabled");

  /* Open input file */
  FILE *input_file = fopen(input_path, "rb");
  if (!input_file) {
    fprintf(stderr, "Error: Cannot open input file '%s'\n", input_path);
    return 1;
  }

  /* Get file size */
  fseek(input_file, 0, SEEK_END);
  long file_size = ftell(input_file);
  fseek(input_file, 0, SEEK_SET);

  int frame_samples = FRAME_SIZE * channels;
  int num_frames = file_size / (frame_samples * sizeof(int16_t));

  printf("File size: %ld bytes\n", file_size);
  printf("Processing: %d frames\n", num_frames);

  /* Configure denoiser */
  struct DenoiserConfig config = {.num_channels = channels,
                                  .model_preset = model_path ? MODEL_CUSTOM
                                                             : MODEL_EMBEDDED,
                                  .model_path = model_path,
                                  .vad_threshold = vad_threshold,
                                  .enable_vad_output = enable_vad};

  /* Create denoiser */
  struct Denoiser denoiser;
  int ret = denoiser_create(&config, &denoiser);
  if (ret != REALTIME_DENOISER_SUCCESS) {
    fprintf(stderr, "Error: Failed to create denoiser (code=%d)\n", ret);
    const char *error = get_denoiser_error(&denoiser);
    if (error) {
      fprintf(stderr, "  %s\n", error);
    }
    fclose(input_file);
    return 1;
  }

  printf("Denoiser initialized\n");

  /* Open output file */
  FILE *output_file = fopen(output_path, "wb");
  if (!output_file) {
    fprintf(stderr, "Error: Cannot create output file '%s'\n", output_path);
    denoiser_destroy(&denoiser);
    fclose(input_file);
    return 1;
  }

  /* Allocate buffers */
  int16_t *input_buffer = malloc(frame_samples * sizeof(int16_t));
  int16_t *output_buffer = malloc(frame_samples * sizeof(int16_t));

  if (!input_buffer || !output_buffer) {
    fprintf(stderr, "Error: Memory allocation failed\n");
    free(input_buffer);
    free(output_buffer);
    denoiser_destroy(&denoiser);
    fclose(input_file);
    fclose(output_file);
    return 1;
  }

  /* Process frames */
  printf("Processing audio...\n");

  /* Start timing for the entire batch */
  struct timespec start_time, end_time;
  clock_gettime(CLOCK_MONOTONIC, &start_time);

  int frame_count = 0;
  while (1) {
    size_t read =
        fread(input_buffer, sizeof(int16_t), frame_samples, input_file);
    if (read == 0) {
      break; /* End of file */
    }

    /* Pad incomplete frames with zeros */
    if (read < (size_t)frame_samples) {
      memset(input_buffer + read, 0, (frame_samples - read) * sizeof(int16_t));
    }

    struct DenoiserResult result;
    ret = denoiser_process(&denoiser, input_buffer, output_buffer, &result);
    if (ret != REALTIME_DENOISER_SUCCESS) {
      fprintf(stderr, "Error processing frame %d\n", frame_count);
      break;
    }

    /* Write only the samples we actually read */
    size_t samples_to_write =
        (read < (size_t)frame_samples) ? read : (size_t)frame_samples;
    fwrite(output_buffer, sizeof(int16_t), samples_to_write, output_file);

    frame_count++;
    if (frame_count % 100 == 0 || read < (size_t)frame_samples) {
      printf("\r    Progress: %d frames processed", frame_count);
      fflush(stdout);
    }
  }

  /* End timing */
  clock_gettime(CLOCK_MONOTONIC, &end_time);
  double total_time_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                         (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;
  double avg_time_ms = frame_count > 0 ? total_time_ms / frame_count : 0.0;

  /* Update denoiser timing stats manually */
  denoiser.total_processing_time = total_time_ms;
  denoiser.last_frame_time = avg_time_ms;

  printf("\n");

  /* Print stats */
  static char stats_buffer[512];
  struct DenoiserStats stats;
  get_denoiser_stats(&denoiser, &stats);

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

  /* Cleanup */
  free(input_buffer);
  free(output_buffer);
  denoiser_destroy(&denoiser);
  fclose(input_file);
  fclose(output_file);

  printf("\nOutput written to: %s\n", output_path);
  return 0;
}
