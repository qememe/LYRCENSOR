#ifndef LY_CENSOR_H
#define LY_CENSOR_H
#include "tokenizer.h"
/* Append a source interval, masking selected, sorted, disjoint token spans. */
bool censor_text(const char *s, size_t n, const Token *t, size_t count, Buffer *out);
#endif
