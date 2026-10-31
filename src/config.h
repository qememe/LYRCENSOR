#ifndef LY_CONFIG_H
#define LY_CONFIG_H
#include "common.h"
#define LY_MODEL_MAX 255
typedef struct { char model[LY_MODEL_MAX+1]; long timeout; double line_threshold, token_threshold; } Config;
void config_init(Config *c);
bool config_valid(const Config *c, char *error);
bool config_key_valid(const char *key);
#endif
