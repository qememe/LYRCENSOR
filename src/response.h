#ifndef LY_RESPONSE_H
#define LY_RESPONSE_H
#include "common.h"
#include <jansson.h>
typedef struct { unsigned long long input_tokens, output_tokens; double cost; size_t token_reports, cost_reports; } Usage;
/* Validate atomically against the exact question keys. probabilities has question count slots. */
bool response_validate(const json_t *response, const json_t *questions, double *probabilities, char *error);
json_t *response_parse(const char *s, size_t n, char *error);
void usage_add(Usage *usage, const json_t *response);
#endif
