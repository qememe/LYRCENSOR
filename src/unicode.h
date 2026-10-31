#ifndef LY_UNICODE_H
#define LY_UNICODE_H
#include "common.h"
#include <stdint.h>
size_t u_decode(const char *s, size_t n, int32_t *cp);
bool u_valid_text(const char *s, size_t n);
bool u_letter(int32_t c);
bool u_mark(int32_t c);
bool u_number(int32_t c);
bool u_space(int32_t c);
size_t u_next(const char *s, size_t n, size_t at);
size_t u_prev(const char *s, size_t n, size_t at);
size_t u_width(const char *s, size_t a, size_t b, size_t column);
#endif
