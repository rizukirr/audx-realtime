#include "audx/audx_realtime.h"
#include "audx/common.h"
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

// Helper function to create a dummy PCM file
static void create_dummy_pcm_file(const char *filename, int num_frames) {
  FILE *f = fopen(filename, "wb");
  TEST_ASSERT_NOT_NULL(f);
  for (int i = 0; i < num_frames * AUDX_DEFAULT_FRAME_SIZE; i++) {
    int16_t sample = (rand() % 1000) - 500;
    fwrite(&sample, sizeof(int16_t), 1, f);
  }
  fclose(f);
}

void test_audx_realtime_stats_enabled(void) {
  const char *input_file = "test_input.pcm";
  const char *output_file = "test_output.pcm";
  create_dummy_pcm_file(input_file, 10);

  struct AudxState state = {
      .input_rate = AUDX_DEFAULT_SAMPLE_RATE,
      .resample_quality = AUDX_DEFAULT_RESAMPLE_QUALITY,
      .model_path = NULL,
      .vad_threshold = AUDX_DEFAULT_VAD_THRESHOLD,
      .stats_enabled = true,
  };

  struct AudxStats stats = {0};
  stats.on_progress = NULL;

  int ret = audx_process_frame(&state, (char *)input_file, (char *)output_file, &stats);
  TEST_ASSERT_EQUAL_INT(AUDX_SUCCESS, ret);

  TEST_ASSERT_TRUE(stats.frame_processed > 0);
  TEST_ASSERT_TRUE(stats.ptime_total > 0);
  TEST_ASSERT_TRUE(stats.vscores_avg > 0);

  remove(input_file);
  remove(output_file);
}

void test_audx_realtime_stats_disabled(void) {
  const char *input_file = "test_input.pcm";
  const char *output_file = "test_output.pcm";
  create_dummy_pcm_file(input_file, 10);

  struct AudxState state = {
      .input_rate = AUDX_DEFAULT_SAMPLE_RATE,
      .resample_quality = AUDX_DEFAULT_RESAMPLE_QUALITY,
      .model_path = NULL,
      .vad_threshold = AUDX_DEFAULT_VAD_THRESHOLD,
      .stats_enabled = false,
  };

  struct AudxStats stats = {0};
  stats.on_progress = NULL;

  int ret = audx_process_frame(&state, (char *)input_file, (char *)output_file, &stats);
  TEST_ASSERT_EQUAL_INT(AUDX_SUCCESS, ret);

  TEST_ASSERT_EQUAL_INT(0, stats.frame_processed);
  TEST_ASSERT_EQUAL_FLOAT(0, stats.ptime_total);
  TEST_ASSERT_EQUAL_FLOAT(0, stats.vscores_avg);

  remove(input_file);
  remove(output_file);
}

void test_audx_realtime_invalid_args(void) {
  int ret = audx_process_frame(NULL, NULL, NULL, NULL);
  TEST_ASSERT_EQUAL_INT(AUDX_ERROR_INVALID, ret);
}

void test_audx_realtime_invalid_input_file(void) {
  struct AudxState state = {0};
  struct AudxStats stats = {0};
  int ret = audx_process_frame(&state, "non_existent_file.pcm", "output.pcm", &stats);
  TEST_ASSERT_EQUAL_INT(AUDX_ERROR_INVALID, ret);
}

void test_audx_realtime_resampling(void) {
  const char *input_file = "test_input_resample.pcm";
  const char *output_file = "test_output_resample.pcm";
  create_dummy_pcm_file(input_file, 10);

  struct AudxState state = {
      .input_rate = 16000, // Trigger resampling
      .resample_quality = AUDX_DEFAULT_RESAMPLE_QUALITY,
      .model_path = NULL,
      .vad_threshold = AUDX_DEFAULT_VAD_THRESHOLD,
      .stats_enabled = false,
  };

  struct AudxStats stats = {0};
  stats.on_progress = NULL;

  int ret = audx_process_frame(&state, (char *)input_file, (char *)output_file, &stats);
  TEST_ASSERT_EQUAL_INT(AUDX_SUCCESS, ret);

  remove(input_file);
  remove(output_file);
}

void test_audx_realtime_invalid_output_path(void) {
  const char *input_file = "test_input_invalid_out.pcm";
  create_dummy_pcm_file(input_file, 1);

  struct AudxState state = {0};
  struct AudxStats stats = {0};

  int ret = audx_process_frame(&state, (char *)input_file, "non_existent_dir/output.pcm", &stats);
  TEST_ASSERT_EQUAL_INT(AUDX_ERROR_INVALID, ret);

  remove(input_file);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_audx_realtime_stats_enabled);
  RUN_TEST(test_audx_realtime_stats_disabled);
  RUN_TEST(test_audx_realtime_invalid_args);
  RUN_TEST(test_audx_realtime_invalid_input_file);
  RUN_TEST(test_audx_realtime_resampling);
  RUN_TEST(test_audx_realtime_invalid_output_path);
  return UNITY_END();
}
