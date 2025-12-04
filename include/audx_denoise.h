#ifndef AUDX_DENOISE_H
#define AUDX_DENOISE_H

/**
 * Opaque structure representing the state of the denoiser.
 */
typedef struct AudxDenoiseState AudxDenoiseState;

/**
 * Create a denoiser.
 *
 * @param model_path    Path to the model file.
 *                      If NULL, the default model is used.
 *
 * @return The denoiser state.
 *
 * The denoiser must be destroyed with audx_denoise_destroy().
 */
AudxDenoiseState *audx_denoise_create(char *model_path);

/**
 * Process a frame of audio.
 *
 * @param state         The denoiser state.
 * @param out           The denoised audio.
 * @param in            The audio to denoise.
 *
 * @return              The probability of speech.
 */
float audx_denoise_process(AudxDenoiseState *state, float *in, float *out);

/**
 * Free a denoiser.
 *
 * @param state The denoiser state.
 */
void audx_denoise_destroy(AudxDenoiseState *state);

#endif // AUDX_DENOISE_H
