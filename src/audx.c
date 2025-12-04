#define ARENA_IMPLEMENTATION
#include "audx.h"
#include "arena.h"
#include "audx_denoise.h"
#include "audx_resampler.h"
#include <stdbool.h>
#include <stdio.h>

// SIMD intrinsics for different architectures
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||             \
    defined(_M_IX86)
#include <emmintrin.h> // SSE2
#include <smmintrin.h> // SSE4.1 for _mm_cvtepi16_epi32
#define HAS_X86_SIMD 1
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#define HAS_ARM_NEON 1
#endif

struct AudxState {
  unsigned int in_rate;
  unsigned int in_len;
  int resample_quality;
  bool need_resample;
  AudxResamplerState *upsampler;
  float *upsampler_buf;
  AudxResamplerState *downsampler;
  float *downsampler_buf;
  AudxDenoiseState *denoiser;
  Arena *arena;
};

AudxState *audx_create(char *model_path, unsigned int in_rate,
                       int resample_quality) {

  Arena *arena = arena_init(16 * 1014);
  if (!arena) {
    return NULL;
  }

  AudxState *state =
      arena_alloc(arena, sizeof(AudxState), ARENA_ALIGNOF(AudxState));
  if (!state) {
    return NULL;
  }

  state->resample_quality = resample_quality;
  state->in_rate = in_rate;
  state->in_len = calculate_frame_sample(in_rate);

  state->need_resample = in_rate != FRAME_RATE;
  if (state->need_resample) {
    state->upsampler =
        audx_resampler_create(in_rate, FRAME_RATE, state->resample_quality);
    if (!state->upsampler) {
      arena_free(arena);
      return NULL;
    }

    state->upsampler_buf =
        arena_alloc(arena, sizeof(float) * FRAME_SIZE, ARENA_ALIGNOF(float));
    if (!state->upsampler_buf) {
      audx_resampler_destroy(state->upsampler);
      arena_free(arena);
      return NULL;
    }

    state->downsampler =
        audx_resampler_create(FRAME_RATE, in_rate, state->resample_quality);
    if (!state->downsampler) {
      audx_resampler_destroy(state->upsampler);
      arena_free(arena);
      return NULL;
    }

    state->downsampler_buf =
        arena_alloc(arena, sizeof(float) * state->in_len, ARENA_ALIGNOF(float));
    if (!state->downsampler_buf) {
      audx_resampler_destroy(state->upsampler);
      audx_resampler_destroy(state->downsampler);
      arena_free(arena);
      return NULL;
    }
  }

  state->denoiser = audx_denoise_create(model_path);
  if (!state->denoiser) {
    if (state->need_resample) {
      audx_resampler_destroy(state->upsampler);
      audx_resampler_destroy(state->downsampler);
    }
    arena_free(arena);
    return NULL;
  }

  state->arena = arena;
  return state;
}

float audx_process_with_resample(AudxState *state, float *in, float *out) {
  if (!state || !out || !in)
    return -1.0;

  unsigned int frame_size = FRAME_SIZE;
  unsigned int in_len = state->in_len;
  int ret = audx_resampler_process(state->upsampler, in, &in_len,
                                   state->upsampler_buf, &frame_size);
  if (ret < 0) {
    return -1.0;
  }
  float vad_prob = audx_denoise_process(state->denoiser, state->upsampler_buf,
                                        state->downsampler_buf);
  if (vad_prob < 0.0) {
    return -1.0;
  }
  ret = audx_resampler_process(state->downsampler, state->downsampler_buf,
                               &frame_size, out, &in_len);
  if (ret < 0) {
    return -1.0;
  }

  return vad_prob;
}

float audx_process(AudxState *state, float *in, float *out) {
  if (!state || !out || !in)
    return -1.0;

  float vad_prob = 0.0;
  if (state->need_resample) {
    vad_prob = audx_process_with_resample(state, in, out);
  } else {
    vad_prob = audx_denoise_process(state->denoiser, in, out);
  }

  return vad_prob;
}

float audx_process_int(AudxState *state, short *in, short *out) {
  if (!state || !in || !out)
    return -1.0;

  float vad_prob = 0.0;
  float tmp_in[state->in_len];
  float tmp_out[FRAME_SIZE];
  pcm_int16_to_float(in, tmp_in, state->in_len);

  if (state->need_resample) {
    vad_prob = audx_process_with_resample(state, tmp_in, tmp_out);
  } else {
    vad_prob = audx_denoise_process(state->denoiser, tmp_in, tmp_out);
  }

  pcm_float_to_int16(tmp_out, out, state->in_len);

  return vad_prob;
}

void audx_destroy(AudxState *state) {
  if (!state)
    return;

  audx_denoise_destroy(state->denoiser);
  if (state->in_rate != FRAME_RATE) {
    audx_resampler_destroy(state->upsampler);
    audx_resampler_destroy(state->downsampler);
  }

  arena_free(state->arena);
}
