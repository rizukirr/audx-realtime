#include "audx/resample.h"
#include "audx/logger.h"
#include <speex/speex_resampler.h>
#include <stdbool.h>

AudxResampler audx_resample_create(audx_uint32_t nb_channels,
                                   audx_uint32_t input_sample_rate,
                                   audx_uint32_t output_sample_rate,
                                   int quality, int *err) {
  int error = 0;
  SpeexResamplerState *speex_state = speex_resampler_init(
      nb_channels, input_sample_rate, output_sample_rate, quality, &error);

  if (!speex_state || error != RESAMPLER_ERR_SUCCESS) {
    AUDX_LOGE("Failed to create resampler: %d\n", error);
    if (err)
      *err = error;
    return NULL;
  }

  /* Skip initial zeros in the resampler filter */
  speex_resampler_skip_zeros(speex_state);

  if (err)
    *err = AUDX_SUCCESS;

  return (AudxResampler)speex_state;
}

int audx_resample_process(AudxResampler resampler, const audx_int16_t *input,
                          audx_uint32_t *input_len, audx_int16_t *output,
                          audx_uint32_t *output_len) {
  if (!resampler || !input || !output || !input_len || !output_len) {
    return AUDX_ERROR_INVALID;
  }

  SpeexResamplerState *speex_state = (SpeexResamplerState *)resampler;

  spx_uint32_t in_len = *input_len;
  spx_uint32_t out_len = *output_len;

  /* Use int16 processing directly - no float conversion needed */
  int ret = speex_resampler_process_int(speex_state, 0, input, &in_len, output,
                                        &out_len);

  if (ret != RESAMPLER_ERR_SUCCESS) {
    AUDX_LOGE("Resampling failed: %d\n", ret);
    return AUDX_ERROR_EXTERNAL;
  }

  *input_len = in_len;
  *output_len = out_len;

  return AUDX_SUCCESS;
}

void audx_resample_destroy(AudxResampler resampler) {
  if (resampler) {
    speex_resampler_destroy((SpeexResamplerState *)resampler);
  }
}
