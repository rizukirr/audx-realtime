#ifndef AUDX_PCM_H
#define AUDX_PCM_H

#ifdef __cplusplus
extern "C" {
#endif

#define PCM_SCALE_FLOAT_MAX 32767.0f
#define PCM_SCALE_FLOAT_MIN -32768.0f

void pcm_int16_to_float(const short *input, float *output, int count);
void pcm_float_to_int16(const float *input, short *output, int count);

#ifdef __cplusplus
}
#endif

#endif // AUDX_PCM_H
