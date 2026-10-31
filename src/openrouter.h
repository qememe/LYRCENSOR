#ifndef LY_OPENROUTER_H
#define LY_OPENROUTER_H
#include "config.h"
#include "response.h"
#include <stdatomic.h>
typedef struct {
    size_t revision;
    unsigned stage;
    size_t lines_total, lines_done, candidates, candidates_done;
    size_t tokens_done, tokens_marked, requests_started, requests_completed, answers;
    Usage usage;
    char activity[LY_ERROR];
} Progress;
typedef void (*ProgressFn)(void *ctx, const Progress *p);
typedef struct {
    const Config *config;
    const char *key; /* borrowed; never copied into diagnostics */
    atomic_bool *cancel;
    Progress *progress;
    ProgressFn notify;
    void *notify_ctx;
    void *handle; /* private reusable CURL handle */
#ifdef LY_TEST_HTTP
    const char *test_url; /* loopback fixture, absent from production builds */
#endif
} ApiClient;
bool api_init(ApiClient *a, char *error);
void api_cleanup(ApiClient *a);
/* request is borrowed; returns only complete, validated probability arrays. */
bool api_decide(void *ctx, const json_t *request, double *probabilities, char *error);
bool api_retry_status(long status);
void progress_notify(Progress *p, ProgressFn fn, void *ctx, const char *activity);
#endif
