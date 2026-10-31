#ifndef LY_EDITOR_H
#define LY_EDITOR_H
#include "common.h"
typedef struct { size_t start, end, next; } Line;
typedef struct { Buffer text; size_t cursor, top, left, goal; bool goal_set; } Editor;
enum { ED_LEFT=256, ED_RIGHT, ED_UP, ED_DOWN, ED_HOME, ED_END, ED_PGUP, ED_PGDN, ED_BACK, ED_DELETE, ED_ENTER };
Line line_at(const char *s, size_t n, size_t start);
size_t editor_start(const Editor *e);
size_t editor_column(const Editor *e);
bool editor_insert(Editor *e, const char *s, size_t n);
void editor_key(Editor *e, int key, size_t page);
#endif
