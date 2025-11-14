#include "audx/audx_realtime.h"
#include "audx/common.h"
#include "audx/denoiser.h"
#include "audx/resample.h"
#include <fcntl.h>
#include <getopt.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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

  struct AudxState st = {
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
