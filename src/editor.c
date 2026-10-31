#include "editor.h"
#include "unicode.h"
Line line_at(const char *s, size_t n, size_t start) {
    Line l = {start, start, start};
    while (l.end < n && s[l.end] != '\n' && s[l.end] != '\r') l.end++;
    l.next = l.end;
    if (l.next < n && s[l.next++] == '\r' && l.next < n && s[l.next] == '\n') l.next++;
    return l;
}
size_t editor_start(const Editor *e) {
    size_t p = e->cursor;
    while (p && e->text.data[p-1] != '\n' && e->text.data[p-1] != '\r') p--;
    return p;
}
size_t editor_column(const Editor *e) {
    size_t c = 0;
    for (size_t p = editor_start(e); p < e->cursor;) {
        size_t q = u_next(e->text.data, e->text.len, p);
        c += u_width(e->text.data, p, q, c); p = q;
    }
    return c;
}
static void align_cursor(Editor *e) {
    /* Insertion/deletion can join CRLF or two previously separate graphemes. */
    if (e->cursor && e->cursor<e->text.len && e->text.data[e->cursor-1]=='\r' && e->text.data[e->cursor]=='\n') e->cursor++;
    size_t start = editor_start(e), p = start;
    while (p < e->cursor) p = u_next(e->text.data, e->text.len, p);
    e->cursor = p;
}
bool editor_insert(Editor *e, const char *s, size_t n) {
    if (!u_valid_text(s, n) || !buf_insert(&e->text, e->cursor, s, n)) return false;
    e->cursor += n;
    align_cursor(e); e->goal_set = false; return true;
}
void editor_key(Editor *e, int key, size_t page) {
    const char *s = e->text.data ? e->text.data : ""; size_t n = e->text.len;
    size_t start = editor_start(e); Line l = line_at(s, n, start);
    bool vertical = key == ED_UP || key == ED_DOWN || key == ED_PGUP || key == ED_PGDN;
    if (vertical) {
        if (!e->goal_set) { e->goal = editor_column(e); e->goal_set = true; }
        size_t steps = (key == ED_PGUP || key == ED_PGDN) ? page : 1;
        while (steps--) {
            if (key == ED_UP || key == ED_PGUP) {
                if (!start) break;
                size_t p = start - 1;
                if (p && s[p] == '\n' && s[p-1] == '\r') p--;
                while (p && s[p-1] != '\n' && s[p-1] != '\r') p--;
                start = p;
            } else { l = line_at(s,n,start); if (l.next == l.end) break; start = l.next; }
        }
        l = line_at(s,n,start); size_t p = start, c = 0;
        while (p < l.end) {
            size_t q = u_next(s, l.end, p), w = u_width(s,p,q,c);
            if (c + w > e->goal) break;
            c += w; p = q;
        }
        e->cursor = p; return;
    }
    e->goal_set = false;
    switch (key) {
    case ED_LEFT: e->cursor = u_prev(s,n,e->cursor); break;
    case ED_RIGHT: e->cursor = u_next(s,n,e->cursor); break;
    case ED_HOME: e->cursor = start; break;
    case ED_END: e->cursor = l.end; break;
    case ED_BACK: if (e->cursor) { size_t p = u_prev(s,n,e->cursor); buf_erase(&e->text,p,e->cursor-p); e->cursor = p; align_cursor(e); } break;
    case ED_DELETE: if (e->cursor < n) { buf_erase(&e->text,e->cursor,u_next(s,n,e->cursor)-e->cursor); align_cursor(e); } break;
    case ED_ENTER: (void)editor_insert(e,"\n",1); break;
    default: break;
    }
}
