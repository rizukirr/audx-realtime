#include "audx_denoise.h"
#include "rnnoise.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct AudxDenoiseState {
  DenoiseState *st;
  RNNModel *model;
};

AudxDenoiseState *audx_denoise_create(char *model_path) {
  DenoiseState *st = NULL;
  RNNModel *model = NULL;

  if (model_path) {
    model = rnnoise_model_from_filename(model_path);
    st = rnnoise_create(model);
  } else {
    st = rnnoise_create(NULL);
  }

  AudxDenoiseState *state = malloc(sizeof(AudxDenoiseState));
  if (!state)
    return NULL;

  state->st = st;
  state->model = model;
  return state;
}

float audx_denoise_process(AudxDenoiseState *state, float *in, float *out) {
  if (!state || !out || !in) {
    return -1.0;
  }

  return rnnoise_process_frame(state->st, out, in);
}

void audx_denoise_destroy(AudxDenoiseState *state) {
  if (!state) {
    return;
  }

  rnnoise_destroy(state->st);
  if (state->model)
    rnnoise_model_free(state->model);

  free(state);
}
