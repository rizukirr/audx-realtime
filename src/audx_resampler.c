#include "audx_resampler.h"
#include "speex/speex_resampler.h"
#include <stdlib.h>

struct AudxResamplerState {
  SpeexResamplerState *st;
};

AudxResamplerState *audx_resampler_create(unsigned int in_rate,
                                          unsigned int out_rate, int quality) {
  AudxResamplerState *st = malloc(sizeof(AudxResamplerState));
  if (!st) {
    return NULL;
  }

  int err = 0;
  st->st = speex_resampler_init(1, in_rate, out_rate, quality, &err);
  if (err != 0) {
    free(st);
    return NULL;
  }
  return st;
}

int audx_resampler_process(AudxResamplerState *st, const float *in,
                           unsigned int *in_len, float *out,
                           unsigned int *out_len) {
  if (!st || !in || !in_len || !out || !out_len) {
    return -1;
  }

  int ret = speex_resampler_process_float(st->st, 0, in, in_len, out, out_len);
  if (ret != 0) {
    return -1;
  }

  return 0;
}

void audx_resampler_destroy(AudxResamplerState *st) {
  if (!st) {
    return;
  }

  speex_resampler_destroy(st->st);
  free(st);
}
