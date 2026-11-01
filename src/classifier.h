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
extern const char classifier_policy_version[];
/* printf templates; expanded task text and metadata share these definitions. */
extern const char classifier_stage1_task[], classifier_stage1_yes[], classifier_stage1_no[];
extern const char classifier_stage2_task[], classifier_stage2_yes[], classifier_stage2_no[];
/* Returns owned JSON metadata, or NULL on allocation failure. */
json_t *classifier_policy_metadata(void);
#endif
