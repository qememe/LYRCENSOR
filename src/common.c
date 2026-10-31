#include "common.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "Linux target requires lock-free signal atomics");
atomic_int ly_signal;
static bool reserve(Buffer *b, size_t n) {
    if (n > LY_LIMIT) return false;
    if (n + 1 <= b->cap) return true;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < n + 1) cap = cap > (LY_LIMIT + 1) / 2 ? LY_LIMIT + 1 : cap * 2;
    char *p = realloc(b->data, cap);
    if (!p) return false;
    b->data = p; b->cap = cap;
    if (!b->len) b->data[0] = 0;
    return true;
}
bool buf_insert(Buffer *b, size_t at, const void *p, size_t n) {
    if (at > b->len || n > LY_LIMIT - b->len || !reserve(b, b->len + n)) return false;
    memmove(b->data + at + n, b->data + at, b->len - at + 1);
    if (n) memcpy(b->data + at, p, n);
    b->len += n; return true;
}
bool buf_append(Buffer *b, const void *p, size_t n) { return buf_insert(b, b->len, p, n); }
void buf_erase(Buffer *b, size_t at, size_t n) {
    if (at > b->len || n > b->len - at) return;
    memmove(b->data + at, b->data + at + n, b->len - at - n + 1); b->len -= n;
}
void buf_free(Buffer *b) { free(b->data); *b = (Buffer){0}; }
char *slice(const char *s, size_t n) {
    if (n > LY_LIMIT) return NULL;
    char *p = malloc(n + 1); if (p) { memcpy(p, s, n); p[n] = 0; } return p;
}
void error_set(char *e, const char *fmt, ...) {
    va_list a; va_start(a, fmt); vsnprintf(e, LY_ERROR, fmt, a); va_end(a);
}
