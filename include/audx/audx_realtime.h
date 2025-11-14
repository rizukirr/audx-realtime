#ifndef AUDX_REALTIME_H
#define AUDX_REALTIME_H

#include "audx/common.h"
#include <stdbool.h>
#include <stdint.h>

struct AudxState {
  audx_int32_t input_rate;
  audx_uint32_t resample_quality;
  char *model_path;
  float vad_threshold;
  bool stats_enabled;
};

typedef void (*AudxProgressCallback)(int progress);

struct AudxStats {
  audx_int32_t input_frame_samples;
  audx_int32_t num_frames;
  long file_size;

  AudxProgressCallback on_progress;

  /** Total number of audio frames processed since initialization */
  int frame_processed;

  /** Percentage of frames where speech was detected (0.0–100.0) */
  float speech_detected;

  /** Average VAD (Voice Activity Detection) score across all frames */
  float vscores_avg;

  /** Minimum VAD score observed */
  float vscores_min;

  /** Maximum VAD score observed */
  float vscores_max;

  /** Total processing time (in milliseconds) accumulated for all frames */
  float ptime_total;

  /** Average processing time per frame (in milliseconds) */
  float ptime_avg;

  /** Processing time (in milliseconds) for the last processed frame */
  float ptime_last;
};

int audx_process_frame(struct AudxState *state, char *input, char *ret_output,
                       struct AudxStats *ret_stats);

void audx_clear_stats(struct AudxStats *stats);

#endif // AUDX_REALTIME_H
