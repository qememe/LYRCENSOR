#ifndef LY_DIAGNOSTICS_H
#define LY_DIAGNOSTICS_H
#include "config.h"
#include <pthread.h>
#include <stdio.h>
/* Records refer to the diagnostics object's OWN immutable source snapshot.
 * Line/token IDs are zero-based; displayed line numbers are one-based. */
typedef struct {
    unsigned stage;
    size_t line_id, token_id, start, end;
    double probability;
    bool selected;
} DiagnosticRecord;
typedef struct {
    char *source;
    size_t source_len;
    Config config;
    pthread_mutex_t lock;
    DiagnosticRecord *records;
    size_t len, cap;
    bool complete;
} Diagnostics;
Diagnostics *diagnostics_create(const char *source, size_t n, const Config *cfg);
void diagnostics_free(Diagnostics *d);
bool diagnostics_record(Diagnostics *d, unsigned stage, size_t line, size_t token,
                        size_t start, size_t end, double probability);
void diagnostics_finish(Diagnostics *d, bool complete);
/* Thread-safe caller-owned snapshot. stage=0: all; 1: lines; 2: tokens.
 * Tokens sort by descending raw probability, ties by line and token ID. */
bool diagnostics_snapshot(Diagnostics *d, unsigned stage, DiagnosticRecord **out,
                          size_t *count, bool *complete);
bool diagnostics_format(const Diagnostics *d, const DiagnosticRecord *r, Buffer *out);
bool diagnostics_print(Diagnostics *d, FILE *stream, char *error);
bool diagnostics_save(Diagnostics *d, int dirfd, char **filename, char *error);
#endif
