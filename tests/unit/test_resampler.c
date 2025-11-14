#include "audx/resample.h"
#include "audx/common.h"
#include "unity.h"
#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846

// Helper to generate a sine wave
void generate_sine_wave(audx_int16_t *buffer, int len, int sample_rate,
                        float freq) {
  for (int i = 0; i < len; i++) {
    buffer[i] = (audx_int16_t)(32767.0f *
                               sinf(2.0f * PI * freq * i / sample_rate));
  }
}

void setUp(void) {}
void tearDown(void) {}

// Test basic resampler creation and destruction
void test_resampler_create_destroy(void) {
  int err = 0;
  AudxResampler resampler =
      audx_resample_create(1, 24000, 48000, AUDX_RESAMPLER_QUALITY_DEFAULT, &err);
  TEST_ASSERT_NOT_NULL(resampler);
  TEST_ASSERT_EQUAL_INT(AUDX_SUCCESS, err);
  audx_resample_destroy(resampler);
}

// Test resampler creation with invalid parameters
void test_resampler_create_invalid_params(void) {
  int err = 0;
  // Invalid quality
  AudxResampler resampler = audx_resample_create(
      1, 24000, 48000, AUDX_RESAMPLER_QUALITY_MAX + 1, &err);
  TEST_ASSERT_NULL(resampler);

  // Invalid sample rate
  resampler = audx_resample_create(1, 0, 48000, AUDX_RESAMPLER_QUALITY_DEFAULT, &err);
  TEST_ASSERT_NULL(resampler);
}

// Test basic upsampling (24kHz -> 48kHz)
void test_resampler_upsample(void) {
  const int input_rate = 24000;
  const int output_rate = 48000;
  const int input_len = 240; // 10ms
  const int output_len_expected = input_len * output_rate / input_rate;

  audx_int16_t input[input_len];
  audx_int16_t output[output_len_expected];
  generate_sine_wave(input, input_len, input_rate, 440.0f);

  int err = 0;
  AudxResampler resampler = audx_resample_create(
      1, input_rate, output_rate, AUDX_RESAMPLER_QUALITY_DEFAULT, &err);
  TEST_ASSERT_NOT_NULL(resampler);
  TEST_ASSERT_EQUAL_INT(AUDX_SUCCESS, err);

  audx_uint32_t in_len = input_len;
  audx_uint32_t out_len = output_len_expected;
  int ret = audx_resample_process(resampler, input, &in_len, output, &out_len);
  TEST_ASSERT_EQUAL_INT(AUDX_SUCCESS, ret);

  // Check that output is not all zeros
  int non_zero_count = 0;
  for (unsigned int i = 0; i < out_len; i++) {
    if (output[i] != 0) {
      non_zero_count++;
    }
  }
  TEST_ASSERT_TRUE(non_zero_count > 0);
  TEST_ASSERT_TRUE(out_len > 0);

  audx_resample_destroy(resampler);
}

// Test basic downsampling (48kHz -> 24kHz)
void test_resampler_downsample(void) {
  const int input_rate = 48000;
  const int output_rate = 24000;
  const int input_len = 480; // 10ms
  const int output_len_expected = input_len * output_rate / input_rate;

  audx_int16_t input[input_len];
  audx_int16_t output[output_len_expected];
  generate_sine_wave(input, input_len, input_rate, 440.0f);

  int err = 0;
  AudxResampler resampler = audx_resample_create(
      1, input_rate, output_rate, AUDX_RESAMPLER_QUALITY_DEFAULT, &err);
  TEST_ASSERT_NOT_NULL(resampler);
  TEST_ASSERT_EQUAL_INT(AUDX_SUCCESS, err);

  audx_uint32_t in_len = input_len;
  audx_uint32_t out_len = output_len_expected;
  int ret = audx_resample_process(resampler, input, &in_len, output, &out_len);
  TEST_ASSERT_EQUAL_INT(AUDX_SUCCESS, ret);

  int non_zero_count = 0;
  for (unsigned int i = 0; i < out_len; i++) {
    if (output[i] != 0) {
      non_zero_count++;
    }
  }
  TEST_ASSERT_TRUE(non_zero_count > 0);
  TEST_ASSERT_TRUE(out_len > 0);

  audx_resample_destroy(resampler);
}

// Test different quality levels
void test_resampler_quality_levels(void) {
  const int input_rate = 24000;
  const int output_rate = 48000;
  const int input_len = 240;
  const int output_len_expected = 480;

  audx_int16_t input[input_len];
  generate_sine_wave(input, input_len, input_rate, 440.0f);

  int qualities[] = {AUDX_RESAMPLER_QUALITY_MIN, AUDX_RESAMPLER_QUALITY_VOIP,
                     AUDX_RESAMPLER_QUALITY_MAX};
  int num_qualities = sizeof(qualities) / sizeof(qualities[0]);

  for (int i = 0; i < num_qualities; i++) {
    int err = 0;
    AudxResampler resampler =
        audx_resample_create(1, input_rate, output_rate, qualities[i], &err);
    TEST_ASSERT_NOT_NULL(resampler);
    TEST_ASSERT_EQUAL_INT(AUDX_SUCCESS, err);

    audx_int16_t output[output_len_expected];
    audx_uint32_t in_len = input_len;
    audx_uint32_t out_len = output_len_expected;
    int ret = audx_resample_process(resampler, input, &in_len, output, &out_len);
    TEST_ASSERT_EQUAL_INT(AUDX_SUCCESS, ret);
    TEST_ASSERT_TRUE(out_len > 0);

    audx_resample_destroy(resampler);
  }
}

// Test resampling with silence
void test_resampler_silence(void) {
  const int input_rate = 24000;
  const int output_rate = 48000;
  const int input_len = 240;
  const int output_len_expected = 480;

  audx_int16_t input[input_len];
  memset(input, 0, sizeof(input));
  audx_int16_t output[output_len_expected];

  int err = 0;
  AudxResampler resampler = audx_resample_create(
      1, input_rate, output_rate, AUDX_RESAMPLER_QUALITY_DEFAULT, &err);
  TEST_ASSERT_NOT_NULL(resampler);

  audx_uint32_t in_len = input_len;
  audx_uint32_t out_len = output_len_expected;
  int ret = audx_resample_process(resampler, input, &in_len, output, &out_len);
  TEST_ASSERT_EQUAL_INT(AUDX_SUCCESS, ret);

  for (unsigned int i = 0; i < out_len; i++) {
    TEST_ASSERT_EQUAL_INT16(0, output[i]);
  }

  audx_resample_destroy(resampler);
}

// Test processing with null pointers
void test_resampler_process_null(void) {
    int err = 0;
    AudxResampler resampler = audx_resample_create(1, 24000, 48000, AUDX_RESAMPLER_QUALITY_DEFAULT, &err);
    TEST_ASSERT_NOT_NULL(resampler);

    audx_int16_t input[240];
    audx_int16_t output[480];
    audx_uint32_t in_len = 240;
    audx_uint32_t out_len = 480;

    TEST_ASSERT_EQUAL_INT(AUDX_ERROR_INVALID, audx_resample_process(NULL, input, &in_len, output, &out_len));
    TEST_ASSERT_EQUAL_INT(AUDX_ERROR_INVALID, audx_resample_process(resampler, NULL, &in_len, output, &out_len));
    TEST_ASSERT_EQUAL_INT(AUDX_ERROR_INVALID, audx_resample_process(resampler, input, NULL, output, &out_len));
    TEST_ASSERT_EQUAL_INT(AUDX_ERROR_INVALID, audx_resample_process(resampler, input, &in_len, NULL, &out_len));
    TEST_ASSERT_EQUAL_INT(AUDX_ERROR_INVALID, audx_resample_process(resampler, input, &in_len, output, NULL));

    audx_resample_destroy(resampler);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_resampler_create_destroy);
  RUN_TEST(test_resampler_create_invalid_params);
  RUN_TEST(test_resampler_upsample);
  RUN_TEST(test_resampler_downsample);
  RUN_TEST(test_resampler_quality_levels);
  RUN_TEST(test_resampler_silence);
  RUN_TEST(test_resampler_process_null);

  return UNITY_END();
}
