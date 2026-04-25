#include "audx_pcm.h"
#include <stdint.h>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||             \
    defined(_M_IX86)
#include <emmintrin.h>
#include <smmintrin.h>
#define HAS_X86_SIMD 1
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#define HAS_ARM_NEON 1
#endif

#ifdef HAS_X86_SIMD
void pcm_int16_to_float(const short *input, float *output, int count) {
  int i = 0;
  for (; i <= count - 8; i += 8) {
    __m128i in16 = _mm_loadu_si128((__m128i *)&input[i]);
    __m128i lo32 = _mm_cvtepi16_epi32(in16);
    __m128i hi32 = _mm_cvtepi16_epi32(_mm_srli_si128(in16, 8));
    __m128 flo = _mm_cvtepi32_ps(lo32);
    __m128 fhi = _mm_cvtepi32_ps(hi32);
    _mm_storeu_ps(&output[i], flo);
    _mm_storeu_ps(&output[i + 4], fhi);
  }
  for (; i < count; i++)
    output[i] = (float)input[i];
}

void pcm_float_to_int16(const float *input, short *output, int count) {
  int i = 0;
  const __m128 max_val = _mm_set1_ps(PCM_SCALE_FLOAT_MAX);
  const __m128 min_val = _mm_set1_ps(PCM_SCALE_FLOAT_MIN);

  for (; i <= count - 8; i += 8) {
    __m128 flo = _mm_loadu_ps(&input[i]);
    __m128 fhi = _mm_loadu_ps(&input[i + 4]);
    flo = _mm_min_ps(_mm_max_ps(flo, min_val), max_val);
    fhi = _mm_min_ps(_mm_max_ps(fhi, min_val), max_val);
    __m128i lo32 = _mm_cvtps_epi32(flo);
    __m128i hi32 = _mm_cvtps_epi32(fhi);
    __m128i packed = _mm_packs_epi32(lo32, hi32);
    _mm_storeu_si128((__m128i *)&output[i], packed);
  }
  for (; i < count; i++) {
    float val = input[i];
    if (val > PCM_SCALE_FLOAT_MAX)
      val = PCM_SCALE_FLOAT_MAX;
    if (val < PCM_SCALE_FLOAT_MIN)
      val = PCM_SCALE_FLOAT_MIN;
    output[i] = (int16_t)val;
  }
}

#elif defined(HAS_ARM_NEON)
void pcm_int16_to_float(const short *input, float *output, int count) {
  int i = 0;
  for (; i <= count - 8; i += 8) {
    int16x8_t in16 = vld1q_s16(&input[i]);
    int32x4_t lo32 = vmovl_s16(vget_low_s16(in16));
    int32x4_t hi32 = vmovl_s16(vget_high_s16(in16));
    float32x4_t flo = vcvtq_f32_s32(lo32);
    float32x4_t fhi = vcvtq_f32_s32(hi32);
    vst1q_f32(&output[i], flo);
    vst1q_f32(&output[i + 4], fhi);
  }
  for (; i < count; i++)
    output[i] = (float)input[i];
}

void pcm_float_to_int16(const float *input, short *output, int count) {
  int i = 0;
  const float32x4_t max_val = vdupq_n_f32(PCM_SCALE_FLOAT_MAX);
  const float32x4_t min_val = vdupq_n_f32(PCM_SCALE_FLOAT_MIN);

  for (; i <= count - 8; i += 8) {
    float32x4_t flo = vld1q_f32(&input[i]);
    float32x4_t fhi = vld1q_f32(&input[i + 4]);
    flo = vminq_f32(vmaxq_f32(flo, min_val), max_val);
    fhi = vminq_f32(vmaxq_f32(fhi, min_val), max_val);
    int32x4_t lo32 = vcvtq_s32_f32(flo);
    int32x4_t hi32 = vcvtq_s32_f32(fhi);
    int16x4_t lo16 = vmovn_s32(lo32);
    int16x4_t hi16 = vmovn_s32(hi32);
    int16x8_t result = vcombine_s16(lo16, hi16);
    vst1q_s16(&output[i], result);
  }
  for (; i < count; i++) {
    float val = input[i];
    if (val > PCM_SCALE_FLOAT_MAX)
      val = PCM_SCALE_FLOAT_MAX;
    if (val < PCM_SCALE_FLOAT_MIN)
      val = PCM_SCALE_FLOAT_MIN;
    output[i] = (int16_t)val;
  }
}

#else
void pcm_int16_to_float(const short *input, float *output, int count) {
  for (int i = 0; i < count; i++)
    output[i] = (float)input[i];
}

void pcm_float_to_int16(const float *input, short *output, int count) {
  for (int i = 0; i < count; i++) {
    float val = input[i];
    if (val > PCM_SCALE_FLOAT_MAX)
      val = PCM_SCALE_FLOAT_MAX;
    if (val < PCM_SCALE_FLOAT_MIN)
      val = PCM_SCALE_FLOAT_MIN;
    output[i] = (int16_t)val;
  }
}
#endif
