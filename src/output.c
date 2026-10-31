#include "output.h"
#include "unicode.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
char *output_basename(const char *s,size_t n,size_t max_bytes) {
    size_t end=0; while (end<n&&s[end]!='\n'&&s[end]!='\r') end++;
    size_t first=0,last=0;
    for (size_t p=0;p<end;) {
        int32_t c; size_t k=u_decode(s+p,end-p,&c); if (!k) return NULL;
        if (!u_space(c)) { if (!last) first=p; last=p+k; }
        p+=k;
    }
    Buffer b={0};
    for (size_t p=first;p<last;) {
        int32_t c; size_t k=u_decode(s+p,last-p,&c); if (!k) goto fail;
        bool unsafe=c<32||c==127||c=='/'||c=='\\'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|'||c==0x202e||c==0x202d||c==0x2066||c==0x2067||c==0x2068||c==0x2069;
        if (b.len+(unsafe?1:k)>max_bytes) break;
        if (!buf_append(&b,unsafe?"_":s+p,unsafe?1:k)) goto fail;
        p+=k;
    }
    /* Avoid hidden names, traversal components, and trailing dots/spaces. */
    while (b.len&&(b.data[b.len-1]=='.'||b.data[b.len-1]==' ')) buf_erase(&b,b.len-1,1);
    if (!b.len || (b.data[0]=='.'&&b.len<3)) {
        buf_free(&b); return slice("lyrics",6);
    }
    if (b.data[0]=='.') b.data[0]='_';
    return b.data;
fail: buf_free(&b); return NULL;
}
static bool save_file(int dirfd,const char *original,size_t n,const char *ext,const Buffer *result,char **filename,char *error) {
    *filename=NULL; long limit=fpathconf(dirfd,_PC_NAME_MAX); if (limit<0||limit>255) limit=255;
    if (limit<32) { error_set(error,"Filesystem filename limit is too small."); return false; }
    char *base=output_basename(original,n,(size_t)limit-24);
    if (!base) { error_set(error,"Cannot allocate safe output filename."); return false; }
    char name[256]; int fd=-1;
    for (unsigned i=0;i<100000;i++) {
        if (i) snprintf(name,sizeof(name),"%s (%u)%s",base,i,ext);
        else snprintf(name,sizeof(name),"%s%s",base,ext);
        fd=openat(dirfd,name,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        if (fd>=0 || errno!=EEXIST) break;
    }
    free(base);
    if (fd<0) { error_set(error,"Cannot create result file: %s.",strerror(errno)); return false; }
    size_t p=0; bool ok=true;
    while (p<result->len) {
        ssize_t k=write(fd,result->data+p,result->len-p);
        if (k<0&&errno==EINTR) continue;
        if (k<=0) { error_set(error,"Cannot write result: %s.",k<0?strerror(errno):"short write"); ok=false; break; }
        p+=(size_t)k;
    }
    if (ok&&fsync(fd)<0) { error_set(error,"Cannot sync result file: %s.",strerror(errno)); ok=false; }
    if (close(fd)<0&&ok) { error_set(error,"Cannot close result file: %s.",strerror(errno)); ok=false; }
    if (ok&&fsync(dirfd)<0&&errno!=EINVAL&&errno!=EROFS) { error_set(error,"Cannot sync output directory: %s.",strerror(errno)); ok=false; }
    if (ok) {
        *filename=slice(name,strlen(name));
        if (!*filename) { error_set(error,"Cannot allocate saved filename."); ok=false; }
    }
    if (!ok) unlinkat(dirfd,name,0);
    return ok;
}
bool output_save(int dirfd,const char *original,size_t n,const Buffer *result,char **filename,char *error) {
    return save_file(dirfd,original,n,".md",result,filename,error);
}
bool output_save_report(int dirfd,const Buffer *report,char **filename,char *error) {
    const char name[]="LYRCENSOR diagnostics";
    return save_file(dirfd,name,sizeof(name)-1,".json",report,filename,error);
}
