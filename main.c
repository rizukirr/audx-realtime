#define AUDX_TIME_IMPL
#include "audx_time.h"

#include "audx.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr,
            "Usage: %s <noisy speech> <output denoised> <sample rate>\n",
            argv[0]);
    return 1;
  }

  uint64_t start = audx_now_ns();

  FILE *f1 = fopen(argv[1], "rb");
  FILE *fout = fopen(argv[2], "wb");
  unsigned int sample_rate = atoi(argv[3]);

  AudxState *state = audx_create(NULL, sample_rate, 4);
  unsigned int in_len = calculate_frame_sample(sample_rate);

  bool first = true;
  short out[in_len];
  short in[in_len];
  float vad_prob = 0;

  size_t frame_processed = 0;
  while (true) {
    short tmp[in_len];
    fread(tmp, sizeof(short), in_len, f1);

    if (feof(f1))
      break;

    for (unsigned int i = 0; i < in_len; i++)
      in[i] = tmp[i];

    float prob = audx_process_int(state, in, out);
    vad_prob += prob;

    for (unsigned int i = 0; i < in_len; i++)
      tmp[i] = out[i];

    if (!first)
      fwrite(tmp, sizeof(short), in_len, fout);

    first = false;
    frame_processed++;

    printf("\rFrame Processed: %zu Time: %f(ms), VAD: %f, denoise prob: %f",
           frame_processed, ((audx_now_ns() - start) / 1e9),
           (vad_prob / frame_processed), prob);
    fflush(stdout);
  }

  printf("\n");
  printf("%zu frame total\n", frame_processed);
  vad_prob /= frame_processed;
  printf("VAD probability: %f\n", vad_prob);
  uint64_t end = audx_now_ns();
  printf("Time: %f ms\n", (end - start) / 1e9);

  audx_destroy(state);
  fclose(fout);
  fclose(f1);

  return 0;
}
