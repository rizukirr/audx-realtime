#ifndef AUDX_TEST_MOCK_DENOISER_H
#define AUDX_TEST_MOCK_DENOISER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct DenoiseState DenoiseState;
typedef struct RNNModel RNNModel;

float mock_vad_value = 0.5f;
bool mock_pcm_called = false;
bool mock_deinterleave_called = false;

void pcm_int16_to_float(const int16_t *in, float *out, int n) {
  mock_pcm_called = true;
  for (int i = 0; i < n; i++)
    out[i] = (float)in[i] / 32768.0f;
}

void pcm_float_to_int16(const float *in, int16_t *out, int n) {
  for (int i = 0; i < n; i++)
    out[i] = (int16_t)(in[i] * 32768.0f);
}

void deinterleave_stereo(const int16_t *in, float *left, float *right, int n) {
  mock_deinterleave_called = true;
  for (int i = 0; i < n; i++) {
    left[i] = (float)in[i * 2] / 32768.0f;
    right[i] = (float)in[i * 2 + 1] / 32768.0f;
  }
}

#endif
