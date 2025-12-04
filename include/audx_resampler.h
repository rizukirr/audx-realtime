#ifndef AUDX_RESAMPLER_H
#define AUDX_RESAMPLER_H

typedef struct AudxResamplerState AudxResamplerState;

#define spx_int16_t short
#define spx_int32_t int
#define spx_uint16_t unsigned short
#define spx_uint32_t unsigned int

AudxResamplerState *audx_resampler_create(unsigned int in_rate,
                                          unsigned int out_rate, int quality);

int audx_resampler_process(AudxResamplerState *st, const float *in,
                           unsigned int *in_len, float *out,
                           unsigned int *out_len);

void audx_resampler_destroy(AudxResamplerState *st);

#endif // AUDX_RESAMPLER_H
