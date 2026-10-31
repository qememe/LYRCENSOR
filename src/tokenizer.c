#include "tokenizer.h"
#include "unicode.h"
#include <stdlib.h>
static bool joiner(int32_t c) { return c == '-' || c == '\'' || c == 0x2019 || c == 0x2010 || c == 0x2011; }
bool split_lines(const char *s, size_t n, Lines *out) {
    *out = (Lines){0}; size_t count = 1;
    for (size_t p=0; p<n;) { Line l=line_at(s,n,p); if (l.next==l.end) break; count++; p=l.next; }
    if (count > LY_LIMIT / sizeof(Line)) return false;
    out->lines=calloc(count,sizeof(Line)); if (!out->lines) return false;
    size_t p=0;
    for (size_t i=0;i<count;i++) { out->lines[i]=line_at(s,n,p); p=out->lines[i].next; }
    out->len=count; return true;
}
bool tokenize_line(const char *s, Line line, size_t line_id, Tokens *out) {
    *out=(Tokens){0}; size_t p=line.start;
    while (p<line.end) {
        int32_t c; size_t k=u_decode(s+p,line.end-p,&c);
        if (!k) { tokens_free(out); return false; }
        if (!u_letter(c) && !u_number(c)) { p+=k; continue; }
        size_t start=p; bool letter=false;
        while (p<line.end) {
            k=u_decode(s+p,line.end-p,&c); if (!k) break;
            if (u_letter(c) || u_number(c) || u_mark(c)) { letter |= u_letter(c); p+=k; continue; }
            if (joiner(c) && p+k<line.end) {
                int32_t next; size_t z=u_decode(s+p+k,line.end-p-k,&next);
                if (z && (u_letter(next) || u_number(next))) { p+=k; continue; }
            }
            break;
        }
        /* Pure numbers carry no redactable letters and are not word tokens. */
        if (!letter) continue;
        if (out->len==out->cap) {
            size_t cap=out->cap ? out->cap*2 : 32;
            if (cap>LY_LIMIT / sizeof(Token)) { tokens_free(out); return false; }
            Token *t=realloc(out->items,cap*sizeof(Token)); if (!t) { tokens_free(out); return false; }
            out->items=t; out->cap=cap;
        }
        out->items[out->len]=(Token){out->len,line_id,start,p,false}; out->len++;
    }
    return true;
}
void tokens_free(Tokens *t) { free(t->items); *t=(Tokens){0}; }
