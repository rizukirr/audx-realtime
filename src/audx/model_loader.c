#include "model_loader.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * @brief Static array of built-in model information
 *
 * Only the embedded model is supported as a preset.
 * Custom models should be loaded directly via file path.
 */
static const struct ModelInfo builtin_models[] = {
    {.preset = MODEL_EMBEDDED,
     .name = "embedded",
     .description = "Built-in RNNoise model (default)",
     .default_path = NULL}};

static const int num_builtin_models =
    sizeof(builtin_models) / sizeof(builtin_models[0]);

/**
 * @brief Check if file exists and is readable
 *
 * @param path Path to file
 * @return true if file exists and is readable
 */
static bool file_exists(const char *path) { return access(path, R_OK) == 0; }

/**
 * @brief Expand tilde (~) in path
 *
 * Replaces leading ~ with user's home directory.
 *
 * @param path          Path potentially containing ~
 * @param buffer        Output buffer for expanded path
 * @param buffer_size   Size of output buffer
 *
 * @return true on success, false on failure
 */
static bool expanded_home_dir(const char *path, char *buffer,
                              size_t buffer_size) {
  if (!path || !buffer || buffer_size == 0)
    return false;

  if (path[0] != '~') {
    // No tilde, just copy
    strncpy(buffer, path, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return true;
  }

  const char *home = getenv("HOME");
  if (!home) {
    // Can't expand without HOME
    return false;
  }

  // Replace ~ with $HOME
  snprintf(buffer, buffer_size, "%s%s", home, path + 1);
  return true;
}

// Public API Implementation
const char *get_model_path_for_preset(enum ModelPreset preset) {
  // Embedded model doesn't need a file path
  if (preset == MODEL_EMBEDDED) {
    return NULL;
  }

  // Custom models should be handled by passing the path directly
  // to the denoiser, not through this function
  return NULL;
}

const struct ModelInfo *get_model_info(enum ModelPreset preset) {
  for (int i = 0; i < num_builtin_models; i++) {
    if (builtin_models[i].preset == preset) {
      return &builtin_models[i];
    }
  }
  return NULL;
}

bool validate_model_file(const char *path) {
  if (!path) {
    return false;
  }

  // Expand ~ if present in path
  char expanded_path[2048];
  if (!expanded_home_dir(path, expanded_path, sizeof(expanded_path))) {
    return false;
  }

  // Check if file exists and is readable
  if (!file_exists(expanded_path)) {
    return false;
  }

  // Check if it's a regular file (not a directory)
  struct stat st;
  if (stat(expanded_path, &st) != 0) {
    return false;
  }

  if (!S_ISREG(st.st_mode)) {
    return false;
  }

  // Check if file is not empty
  if (st.st_size == 0) {
    return false;
  }

  return true;
}

const struct ModelInfo *get_all_models(int *count) {
  if (count) {
    *count = num_builtin_models;
  }
  return builtin_models;
}

void list_available_models(void) {
  printf("\nAvailable RNNoise Models:\n\n");
  printf("  embedded  Built-in RNNoise model (default)\n");
  printf("            Status: ✓ Always available\n");
  printf("            Usage:  --denoise\n\n");
  printf("  custom    User-trained model in binary blob format\n");
  printf("            Usage:  --denoise --denoise-model=/path/to/model.bin\n");
  printf("            Info:   See assets/models/rnnoise/README.md for "
         "training\n\n");
}

enum ModelPreset get_preset_from_name(const char *name) {
  if (!name) {
    return MODEL_EMBEDDED; // Default to embedded
  }

  // Convert to lowercase for comparison
  char lowercase[32];
  size_t len = strlen(name);
  if (len >= sizeof(lowercase)) {
    len = sizeof(lowercase) - 1;
  }

  for (size_t i = 0; i < len; i++) {
    lowercase[i] = tolower(name[i]);
  }
  lowercase[len] = '\0';

  // Check for "embedded"
  if (strcmp(lowercase, "embedded") == 0) {
    return MODEL_EMBEDDED;
  }

  // Anything else is treated as a custom model path
  return MODEL_CUSTOM;
}
