#include "audx/common.h"
#include "audx/denoiser.h"
#include "unity.h"
#include <stdlib.h>
#include <string.h>

// Global result for callback to populate
static struct DenoiserResult test_result;
static bool callback_called;

void setUp(void) {
  callback_called = false;
  memset(&test_result, 0, sizeof(test_result));
}
void tearDown(void) {}

void test_denoiser_create(void) {
  struct DenoiserConfig config = {.model_preset = MODEL_EMBEDDED,
                                  .model_path = NULL,
                                  .vad_threshold = 0.5,
                                  .stats_enabled = false};

  struct Denoiser ds;
  memset(&ds, 0, sizeof(ds));
  int ret = denoiser_create(&config, &ds);
  TEST_ASSERT_EQUAL_INT(0, ret);
  denoiser_destroy(&ds);
}

void test_denoiser_create_no_config(void) {
  struct DenoiserConfig config = {};
  struct Denoiser ds;
  memset(&ds, 0, sizeof(ds));
  int ret = denoiser_create(&config, &ds);
  TEST_ASSERT_EQUAL_INT(0, ret);
  TEST_ASSERT_EQUAL_INT(1, ds.num_channels);
  TEST_ASSERT_EQUAL_FLOAT(0.5, ds.vad_threshold);
  TEST_ASSERT_EQUAL_INT(1, ds.min_vad_score);
  TEST_ASSERT_EQUAL_INT(0, ds.max_vad_score);
  denoiser_destroy(&ds);
}

void test_denoiser_create_null_config(void) {
  struct Denoiser ds;
  memset(&ds, 0, sizeof(ds));
  int ret = denoiser_create(NULL, &ds);
  TEST_ASSERT_EQUAL_INT(-1, ret);
  denoiser_destroy(&ds);
}

void test_denoiser_process(void) {
  struct DenoiserConfig config = {};
  struct Denoiser ds;
  memset(&ds, 0, sizeof(ds));
  int ret = denoiser_create(&config, &ds);
  TEST_ASSERT_EQUAL_INT(0, ret);

  for (int frame = 0; frame < 10; frame++) {
    // Generate fake input (silence with noise)
    int16_t input[480];
    for (int i = 0; i < 480; i++) {
      input[i] = (rand() % 1000) - 500; // Random noise
    }

    // Process
    int16_t output[480];
    int ret = denoiser_process(&ds, input, output, NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
  }
  denoiser_destroy(&ds);
}

void test_denoiser_process_null_param(void) {
  int ret = denoiser_process(NULL, NULL, NULL, NULL);
  TEST_ASSERT_EQUAL_INT(-1, ret);
}

// Test that only mono channel is supported
void test_denoiser_mono_only(void) {
  struct DenoiserConfig config = {.model_preset = MODEL_EMBEDDED,
                                  .model_path = NULL,
                                  .vad_threshold = 0.5,
                                  .stats_enabled = false};

  struct Denoiser ds;
  memset(&ds, 0, sizeof(ds));
  int ret = denoiser_create(&config, &ds);
  TEST_ASSERT_EQUAL_INT(0, ret);
  // Verify it's mono (1 channel)
  TEST_ASSERT_EQUAL_INT(1, ds.num_channels);
  denoiser_destroy(&ds);
}

// Test VAD functionality
void test_denoiser_vad_enabled(void) {
  struct DenoiserConfig config = {.model_preset = MODEL_EMBEDDED,
                                  .model_path = NULL,
                                  .vad_threshold = 0.5,
                                  .stats_enabled = true};

  struct Denoiser ds;
  memset(&ds, 0, sizeof(ds));
  int ret = denoiser_create(&config, &ds);
  TEST_ASSERT_EQUAL_INT(0, ret);

  int16_t input[480];
  int16_t output[480];

  // Process a frame
  for (int i = 0; i < 480; i++) {
    input[i] = (rand() % 1000) - 500;
  }

  struct DenoiserResult result;
  ret = denoiser_process(&ds, input, output, &result);
  TEST_ASSERT_EQUAL_INT(0, ret);
  TEST_ASSERT_TRUE(callback_called);

  // Check that VAD probability is in valid range
  TEST_ASSERT_TRUE(test_result.vad_probability >= 0.0f);
  TEST_ASSERT_TRUE(test_result.vad_probability <= 1.0f);
  TEST_ASSERT_EQUAL_INT(480, test_result.samples_processed);

  denoiser_destroy(&ds);
}

void test_denoiser_vad_threshold(void) {
  struct DenoiserConfig config = {.model_preset = MODEL_EMBEDDED,
                                  .model_path = NULL,
                                  .vad_threshold = 0.9, // High threshold
                                  .stats_enabled = true};

  struct Denoiser ds;
  memset(&ds, 0, sizeof(ds));
  int ret = denoiser_create(&config, &ds);
  TEST_ASSERT_EQUAL_INT(0, ret);
  TEST_ASSERT_EQUAL_FLOAT(0.9, ds.vad_threshold);

  denoiser_destroy(&ds);
}

// Test statistics tracking
void test_denoiser_stats(void) {
  struct DenoiserConfig config = {.model_preset = MODEL_EMBEDDED,
                                  .model_path = NULL,
                                  .vad_threshold = 0.5,
                                  .stats_enabled = true};

  struct Denoiser ds;
  memset(&ds, 0, sizeof(ds));
  int ret = denoiser_create(&config, &ds);
  TEST_ASSERT_EQUAL_INT(0, ret);

  // Process multiple frames
  for (int frame = 0; frame < 5; frame++) {
    int16_t input[480];
    int16_t output[480];

    for (int i = 0; i < 480; i++) {
      input[i] = (rand() % 1000) - 500;
    }
    struct DenoiserResult result;
    ret = denoiser_process(&ds, input, output, &result);
    TEST_ASSERT_EQUAL_INT(0, ret);
  }

  // Get statistics
  struct DenoiserStats stats;
  ret = get_denoiser_stats(&ds, &stats);
  TEST_ASSERT_TRUE(ret == 0);

  float avg_vad = (ds.frames_processed > 0)
                      ? (ds.total_vad_score / ds.frames_processed)
                      : 0.0f;
  float speech_percent = (ds.frames_processed > 0)
                             ? (100.0f * ds.speech_frames / ds.frames_processed)
                             : 0.0f;

  double avg_frame_time = (ds.frames_processed > 0)
                              ? (ds.total_processing_time / ds.frames_processed)
                              : 0.0;

  TEST_ASSERT_EQUAL_INT((int)ds.frames_processed, stats.frame_processed);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, avg_frame_time, stats.ptime_avg);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, speech_percent, stats.speech_detected);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, avg_vad, stats.vscores_avg);
  TEST_ASSERT_EQUAL_FLOAT(ds.max_vad_score, stats.vscores_max);
  TEST_ASSERT_EQUAL_FLOAT(ds.last_frame_time, stats.ptime_last);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, ds.total_processing_time, stats.ptime_total);
  TEST_ASSERT_EQUAL_FLOAT(ds.min_vad_score, stats.vscores_min);

  denoiser_destroy(&ds);
}

void test_denoiser_stats_null(void) {
  int ret = get_denoiser_stats(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_denoiser_frame_counting(void) {
  struct DenoiserConfig config = {.model_preset = MODEL_EMBEDDED,
                                  .model_path = NULL,
                                  .vad_threshold = 0.5,
                                  .stats_enabled = false};

  struct Denoiser ds;
  memset(&ds, 0, sizeof(ds));
  int ret = denoiser_create(&config, &ds);
  TEST_ASSERT_EQUAL_INT(0, ret);

  // Process 10 frames
  for (int frame = 0; frame < 10; frame++) {
    int16_t input[480];
    int16_t output[480];

    for (int i = 0; i < 480; i++) {
      input[i] = (rand() % 1000) - 500;
    }

    ret = denoiser_process(&ds, input, output, NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
  }

  // Check frame count through stats
  TEST_ASSERT_EQUAL_UINT64(0, ds.frames_processed);

  denoiser_destroy(&ds);
}

// Test error handling
// Note: This test is removed as the library only supports mono channel
// and invalid channel configurations are not applicable

void test_denoiser_error_null(void) {
  const char *error = get_denoiser_error(NULL);
  TEST_ASSERT_NULL(error);
}

void test_denoiser_process_null_input(void) {
  struct DenoiserConfig config = {.model_preset = MODEL_EMBEDDED,
                                  .model_path = NULL,
                                  .vad_threshold = 0.5,
                                  .stats_enabled = false};

  struct Denoiser ds;
  memset(&ds, 0, sizeof(ds));
  int ret = denoiser_create(&config, &ds);
  TEST_ASSERT_EQUAL_INT(0, ret);

  int16_t output[480];
  ret = denoiser_process(&ds, NULL, output, NULL);
  TEST_ASSERT_EQUAL_INT(AUDX_ERROR_INVALID, ret);

  denoiser_destroy(&ds);
}

void test_denoiser_process_null_output(void) {
  struct DenoiserConfig config = {.model_preset = MODEL_EMBEDDED,
                                  .model_path = NULL,
                                  .vad_threshold = 0.5,
                                  .stats_enabled = false};

  struct Denoiser ds;
  memset(&ds, 0, sizeof(ds));
  int ret = denoiser_create(&config, &ds);
  TEST_ASSERT_EQUAL_INT(0, ret);

  int16_t input[480];
  ret = denoiser_process(&ds, input, NULL, NULL);
  TEST_ASSERT_EQUAL_INT(AUDX_ERROR_INVALID, ret);

  denoiser_destroy(&ds);
}

// Test utility functions
void test_denoiser_version(void) {
  const char *version = denoiser_version();
  TEST_ASSERT_NOT_NULL(version);
  TEST_ASSERT_TRUE(strlen(version) > 0);
  TEST_ASSERT_EQUAL_STRING("1.0.0", version);
}

void test_denoiser_destroy_null(void) {
  // Should not crash
  denoiser_destroy(NULL);
  TEST_ASSERT_TRUE(true); // If we get here, test passed
}

// Test VAD score tracking over multiple frames
void test_denoiser_vad_score_tracking(void) {
  struct DenoiserConfig config = {.model_preset = MODEL_EMBEDDED,
                                  .model_path = NULL,
                                  .vad_threshold = 0.5,
                                  .stats_enabled = true};

  struct Denoiser ds;
  memset(&ds, 0, sizeof(ds));
  int ret = denoiser_create(&config, &ds);
  TEST_ASSERT_EQUAL_INT(0, ret);

  // Initial min/max should be set to 1.0 and 0.0
  TEST_ASSERT_EQUAL_FLOAT(1.0f, ds.min_vad_score);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, ds.max_vad_score);

  // Process frames
  for (int frame = 0; frame < 5; frame++) {
    int16_t input[480];
    int16_t output[480];

    for (int i = 0; i < 480; i++) {
      input[i] = (rand() % 1000) - 500;
    }

    struct DenoiserResult result;
    ret = denoiser_process(&ds, input, output, &result);
    TEST_ASSERT_EQUAL_INT(0, ret);
  }

  // After processing, min should be <= max
  TEST_ASSERT_TRUE(ds.min_vad_score <= ds.max_vad_score);
  TEST_ASSERT_TRUE(ds.min_vad_score >= 0.0f);
  TEST_ASSERT_TRUE(ds.max_vad_score <= 1.0f);

  denoiser_destroy(&ds);
}

// Test result with VAD disabled
void test_denoiser_vad_disabled(void) {
  struct DenoiserConfig config = {.model_preset = MODEL_EMBEDDED,
                                  .model_path = NULL,
                                  .vad_threshold = 0.5,
                                  .stats_enabled = false};

  struct Denoiser ds;
  memset(&ds, 0, sizeof(ds));
  int ret = denoiser_create(&config, &ds);
  TEST_ASSERT_EQUAL_INT(0, ret);

  int16_t input[480];
  int16_t output[480];

  for (int i = 0; i < 480; i++) {
    input[i] = (rand() % 1000) - 500;
  }

  struct DenoiserResult result;
  ret = denoiser_process(&ds, input, output, &result);
  TEST_ASSERT_EQUAL_INT(0, ret);

  // With stats disabled, the result is still populated, but internal stats are
  // not tracked.
  TEST_ASSERT_TRUE(callback_called);
  TEST_ASSERT_EQUAL_INT(480, test_result.samples_processed);

  denoiser_destroy(&ds);
}

// Main function to run tests

// Test custom model loading
void test_denoiser_custom_model_load_success(void) {
  const char *dummy_model_path = "tests/assets/weights_blob.bin";

  struct DenoiserConfig config = {.model_preset = MODEL_CUSTOM,
                                  .model_path = dummy_model_path,
                                  .vad_threshold = 0.5,
                                  .stats_enabled = false};

  struct Denoiser ds;
  memset(&ds, 0, sizeof(ds));
  int ret = denoiser_create(&config, &ds);
  TEST_ASSERT_EQUAL_INT(0, ret); // Expect success
  denoiser_destroy(&ds);
}

// Test custom model loading with non-existent file
void test_denoiser_custom_model_load_fail_non_existent(void) {
  struct DenoiserConfig config = {.model_preset = MODEL_CUSTOM,
                                  .model_path = "non_existent_model.rnnn",
                                  .vad_threshold = 0.5,
                                  .stats_enabled = false};

  struct Denoiser ds;
  memset(&ds, 0, sizeof(ds));
  int ret = denoiser_create(&config, &ds);
  TEST_ASSERT_EQUAL_INT(AUDX_ERROR_INVALID, ret); // Expect failure
}

// Test custom model loading with empty file
void test_denoiser_custom_model_load_fail_empty(void) {
  const char *empty_model_path = "empty_model.rnnn";
  FILE *f = fopen(empty_model_path, "wb");
  TEST_ASSERT_NOT_NULL(f);
  fclose(f); // Create an empty file

  struct DenoiserConfig config = {.model_preset = MODEL_CUSTOM,
                                  .model_path = empty_model_path,
                                  .vad_threshold = 0.5,
                                  .stats_enabled = false};

  struct Denoiser ds;
  memset(&ds, 0, sizeof(ds));
  int ret = denoiser_create(&config, &ds);
  TEST_ASSERT_EQUAL_INT(AUDX_ERROR_INVALID, ret); // Expect failure
  remove(empty_model_path);
}

int main(void) {
  UNITY_BEGIN();

  // Basic creation tests
  RUN_TEST(test_denoiser_create);
  RUN_TEST(test_denoiser_create_no_config);
  RUN_TEST(test_denoiser_create_null_config);
  RUN_TEST(test_denoiser_mono_only);

  // Processing tests
  RUN_TEST(test_denoiser_process);
  RUN_TEST(test_denoiser_process_null_param);
  RUN_TEST(test_denoiser_process_null_input);
  RUN_TEST(test_denoiser_process_null_output);

  // VAD tests
  RUN_TEST(test_denoiser_vad_enabled);
  RUN_TEST(test_denoiser_vad_disabled);
  RUN_TEST(test_denoiser_vad_threshold);
  RUN_TEST(test_denoiser_vad_score_tracking);

  // Statistics tests
  RUN_TEST(test_denoiser_stats);
  RUN_TEST(test_denoiser_stats_null);
  RUN_TEST(test_denoiser_frame_counting);

  // Model loading tests
  RUN_TEST(test_denoiser_custom_model_load_success);
  RUN_TEST(test_denoiser_custom_model_load_fail_non_existent);
  RUN_TEST(test_denoiser_custom_model_load_fail_empty);

  // Error handling tests
  RUN_TEST(test_denoiser_error_null);

  // Utility tests
  RUN_TEST(test_denoiser_version);
  RUN_TEST(test_denoiser_destroy_null);

  return UNITY_END();
}
