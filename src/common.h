#ifndef LY_COMMON_H
#define LY_COMMON_H
#include <stdbool.h>
#include <stddef.h>
#include <signal.h>
#include <stdatomic.h>
#define LY_LIMIT (16u * 1024u * 1024u)
#define LY_ERROR 256
/* Buffers own data; all public strings are NUL terminated. len excludes NUL. */
typedef struct { char *data; size_t len, cap; } Buffer;
bool buf_append(Buffer *b, const void *p, size_t n);
bool buf_insert(Buffer *b, size_t at, const void *p, size_t n);
void buf_erase(Buffer *b, size_t at, size_t n);
void buf_free(Buffer *b);
char *slice(const char *s, size_t n);
void error_set(char *e, const char *fmt, ...);
/* Lock-free atomic reads are shared by the signal handler, UI and worker. */
extern atomic_int ly_signal;
#endif
