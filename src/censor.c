#include "censor.h"
#include "unicode.h"
bool censor_text(const char *s, size_t n, const Token *t, size_t count, Buffer *out) {
    *out=(Buffer){0}; size_t p=0;
    for (size_t i=0;i<count;i++) {
        if (t[i].start<p || t[i].end<t[i].start || t[i].end>n) goto fail;
        if (!buf_append(out,s+p,t[i].start-p)) goto fail;
        bool removed_base=false; p=t[i].start;
        while (p<t[i].end) {
            int32_t c; size_t k=u_decode(s+p,t[i].end-p,&c); if (!k) goto fail;
            if (t[i].selected && u_letter(c)) {
                if (!buf_append(out,"*",1)) goto fail;
                removed_base=true;
            } else if (!(t[i].selected && removed_base && u_mark(c))) {
                if (!buf_append(out,s+p,k)) goto fail;
                if (!u_mark(c)) removed_base=false;
            }
            p+=k;
        }
    }
    if (!buf_append(out,s+p,n-p)) goto fail;
    return true;
fail: buf_free(out); return false;
}
