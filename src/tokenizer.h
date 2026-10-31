#ifndef LY_TOKENIZER_H
#define LY_TOKENIZER_H
#include "editor.h"
/* Tokens are borrowed byte spans into immutable source; end is exclusive.
 * IDs are zero-based within each logical line. The pair (line_id,id) is stable. */
typedef struct { size_t id, line_id, start, end; bool selected; } Token;
typedef struct { Token *items; size_t len, cap; } Tokens;
typedef struct { Line *lines; size_t len; } Lines;
bool split_lines(const char *s, size_t n, Lines *out);
bool tokenize_line(const char *s, Line line, size_t line_id, Tokens *out);
void tokens_free(Tokens *t);
#endif
