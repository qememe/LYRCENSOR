#include "unicode.h"
#include <utf8proc.h>
size_t u_decode(const char *s, size_t n, int32_t *cp) {
    if (!n) return 0;
    utf8proc_ssize_t r = utf8proc_iterate((const utf8proc_uint8_t *)s, (utf8proc_ssize_t)n, cp);
    return r > 0 ? (size_t)r : 0;
}
bool u_valid_text(const char *s, size_t n) {
    for (size_t p = 0; p < n;) {
        int32_t c; size_t k = u_decode(s + p, n - p, &c);
        /* Terminal controls can execute OSC/CSI on final stdout. Reject, never strip. */
        if (!k || !c || (c < 32 && c != '\t' && c != '\n' && c != '\r') || (c >= 127 && c <= 159)) return false;
        p += k;
    }
    return true;
}
bool u_letter(int32_t c) { int t = utf8proc_category(c); return t >= UTF8PROC_CATEGORY_LU && t <= UTF8PROC_CATEGORY_LO; }
bool u_mark(int32_t c) { int t = utf8proc_category(c); return t >= UTF8PROC_CATEGORY_MN && t <= UTF8PROC_CATEGORY_ME; }
bool u_number(int32_t c) { int t = utf8proc_category(c); return t >= UTF8PROC_CATEGORY_ND && t <= UTF8PROC_CATEGORY_NO; }
bool u_space(int32_t c) { return c == 9 || c == 10 || c == 13 || utf8proc_category(c) == UTF8PROC_CATEGORY_ZS || utf8proc_category(c) == UTF8PROC_CATEGORY_ZL || utf8proc_category(c) == UTF8PROC_CATEGORY_ZP; }
size_t u_next(const char *s, size_t n, size_t at) {
    if (at >= n) return n;
    int32_t prev, c, state = 0; size_t k = u_decode(s + at, n - at, &prev);
    if (!k) return at + 1;
    size_t p = at + k;
    while (p < n) {
        k = u_decode(s + p, n - p, &c);
        if (!k || utf8proc_grapheme_break_stateful(prev, c, &state)) break;
        prev = c; p += k;
    }
    return p;
}
size_t u_prev(const char *s, size_t n, size_t at) {
    size_t p = 0, last = 0;
    while (p < at && p < n) { last = p; p = u_next(s, n, p); }
    return last;
}
size_t u_width(const char *s, size_t a, size_t b, size_t column) {
    if (s[a] == '\t') return 8 - column % 8;
    size_t width = 0; bool emoji = false;
    while (a < b) {
        int32_t c; size_t k = u_decode(s + a, b - a, &c); if (!k) return 1;
        int w = utf8proc_charwidth(c);
        if (c == 0xfe0f || c == 0x20e3) emoji = true;
        if (w > 0 && (size_t)w > width) width = (size_t)w;
        a += k;
    }
    if (emoji && width < 2) width = 2;
    return width;
}
