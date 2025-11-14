#ifndef AUDX_RESAMPLER_H
#define AUDX_RESAMPLER_H

#include "audx/common.h"
#include <speex/speex_resampler.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDX_RESAMPLER_QUALITY_MAX 10
#define AUDX_RESAMPLER_QUALITY_MIN 0
#define AUDX_RESAMPLER_QUALITY_DEFAULT 4
#define AUDX_RESAMPLER_QUALITY_VOIP 3

struct AudxResamplerState {
  audx_uint32_t nb_channels;
  audx_uint32_t input_sample_rate;
  audx_uint32_t output_sample_rate;
  int quality;
  const audx_int16_t *input_int16;
  audx_uint32_t input_len;
  audx_int16_t *output_int16;
  audx_uint32_t output_len;
  audx_uint32_t *actual_output_len;
};

/**
 * @brief Opaque resampler handle
 */
typedef void *AudxResampler;

/**
 * @brief Creates a persistent resampler instance
 *
 * @param nb_channels         Number of audio channels (1 for mono)
 * @param input_sample_rate   Input sampling rate in Hz
 * @param output_sample_rate  Output sampling rate in Hz
 * @param quality             Resampling quality (0-10, where 10 is best)
 * @param err                 Output error code
 *
 * @return Opaque resampler handle, or NULL on failure
 */
AudxResampler audx_resample_create(audx_uint32_t nb_channels,
                                   audx_uint32_t input_sample_rate,
                                   audx_uint32_t output_sample_rate,
                                   int quality, int *err);

/**
 * @brief Process audio samples through the resampler
 *
 * @param resampler           Resampler handle from audx_resample_create
 * @param input               Input audio samples (int16)
 * @param input_len           Number of input samples (updated with consumed
 * count)
 * @param output              Output buffer for resampled audio
 * @param output_len          Size of output buffer (updated with written count)
 *
 * @return AUDX_RESAMPLER_SUCCESS on success, negative error code on failure
 */
int audx_resample_process(AudxResampler resampler, const audx_int16_t *input,
                          audx_uint32_t *input_len, audx_int16_t *output,
                          audx_uint32_t *output_len);

/**
 * @brief Destroys a resampler instance
 *
 * @param resampler           Resampler handle to destroy
 */
void audx_resample_destroy(AudxResampler resampler);

#ifdef __cplusplus
}
#endif

#endif // AUDX_RESAMPLER_H
