#ifndef LY_CLASSIFIER_H
#define LY_CLASSIFIER_H
#include "openrouter.h"
#include "censor.h"
#include "diagnostics.h"
/* Decision function must return a fully validated probability for each question,
 * in question insertion order. Injection is for offline tests; production uses api_decide. */
typedef bool (*DecisionFn)(void *, const json_t *, double *, char *);
bool classifier_run(const char *source, size_t n, const Config *cfg, Diagnostics *diagnostics,
                    atomic_bool *cancel, Progress *progress, ProgressFn notify,
                    void *notify_ctx, DecisionFn decide, void *decide_ctx,
                    Buffer *result, char *error);
extern const char classifier_policy[];
#endif
