#include "support.h"
#include <arpa/inet.h>
#include <jansson.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
void test_fail(const char *file,int line,const char *expr) { fprintf(stderr,"FAIL %s:%d: %s\n",file,line,expr); abort(); }
void test_sleep(unsigned ms) { struct timespec t={(time_t)(ms/1000),(long)(ms%1000)*1000000}; while (nanosleep(&t,&t)<0&&errno==EINTR) {} }
void write_all(int fd,const char *s,size_t n) { while (n) { ssize_t k=write(fd,s,n); if (k<0&&errno==EINTR) continue; T(k>0); s+=(size_t)k; n-=(size_t)k; } }
char *read_file(const char *path,size_t *len) {
    int fd=open(path,O_RDONLY); if (fd<0) return NULL;
    struct stat st; T(fstat(fd,&st)==0&&st.st_size>=0); *len=(size_t)st.st_size; char *b=malloc(*len+1); T(b);
    size_t p=0; while (p<*len) { ssize_t n=read(fd,b+p,*len-p); T(n>0); p+=(size_t)n; }
    b[p]=0; close(fd); return b;
}
static json_t *read_request(int fd) {
    char b[65536]; size_t len=0,head=0,body=0;
    while (len<sizeof(b)-1) {
        ssize_t n=read(fd,b+len,sizeof(b)-len-1); T(n>0); len+=(size_t)n; b[len]=0;
        char *end=strstr(b,"\r\n\r\n");
        if (!head&&end) {
            head=(size_t)(end-b)+4; char *length=strstr(b,"Content-Length:"); T(length); body=(size_t)strtoul(length+15,NULL,10);
            T(strstr(b,"POST /api/alpha/decisions HTTP/")); T(strstr(b,"Authorization: Bearer fixture-only"));
        }
        if (head&&len>=head+body) break;
    }
    T(head&&len>=head+body); json_error_t error; json_t *j=json_loadb(b+head,body,JSON_REJECT_DUPLICATES,&error); T(j);
    T(json_object_size(j)==3); T(json_is_object(json_object_get(j,"questions"))); T(json_object_get(j,"state"));
    T(!strcmp(json_string_value(json_object_get(j,"model")),"typesafe/jev-1.13"));
    const char *key; json_t *q; json_object_foreach(json_object_get(j,"questions"),key,q) { (void)key; T(!strcmp(json_string_value(json_object_get(q,"type")),"noul")); }
    return j;
}
static void serve(int listener,const Fixture *f,size_t count) {
    signal(SIGPIPE,SIG_IGN); alarm(30);
    for (size_t i=0;i<count;i++) {
        int fd=accept(listener,NULL,NULL); T(fd>=0); json_t *req=read_request(fd);
        const char *body=f[i].body?f[i].body:"{}"; char *generated=NULL;
        if (f[i].dynamic) {
            json_t *answers=json_object(); T(answers); const char *key; json_t *q;
            json_object_foreach(json_object_get(req,"questions"),key,q) { (void)q; T(json_object_set_new(answers,key,json_pack("{s:s,s:f}","type","noul","noul",1.0))==0); }
            json_t *r=json_pack("{s:s,s:o,s:{s:i,s:i,s:f}}","model","typesafe/jev-1.13","answers",answers,"usage","input_tokens",100,"output_tokens",0,"cost",0.000123);
            T(r); generated=json_dumps(r,JSON_COMPACT); T(generated); json_decref(r); body=generated;
        }
        json_decref(req); test_sleep(f[i].delay_ms);
        char header[512]; size_t n=strlen(body);
        int k=snprintf(header,sizeof(header),"HTTP/1.1 %d Fixture\r\nContent-Type: %s\r\nContent-Length: %zu\r\nRetry-After: 0\r\nConnection: close\r\n\r\n",f[i].status,f[i].type?f[i].type:"application/json",n+(f[i].truncate?10u:0u)); T(k>0);
        /* Deliberately fragment headers/body across writes. Cancellation can close the socket. */
        if (send(fd,header,(size_t)k,MSG_NOSIGNAL)>0) {
            for (size_t p=0;p<n;) { size_t bytes=n-p>11?11:n-p; ssize_t r=send(fd,body+p,bytes,MSG_NOSIGNAL); if (r<=0) break; p+=(size_t)r; }
        }
        free(generated); close(fd);
    }
    close(listener); _exit(0);
}
Server server_start(const Fixture *f,size_t count) {
    int fd=socket(AF_INET,SOCK_STREAM,0); T(fd>=0); int one=1; T(setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one))==0);
    struct sockaddr_in addr={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    T(bind(fd,(struct sockaddr *)&addr,sizeof(addr))==0); T(listen(fd,8)==0); socklen_t n=sizeof(addr); T(getsockname(fd,(struct sockaddr *)&addr,&n)==0);
    Server s={0}; snprintf(s.url,sizeof(s.url),"http://127.0.0.1:%u/api/alpha/decisions",ntohs(addr.sin_port));
    s.pid=fork(); T(s.pid>=0); if (!s.pid) serve(fd,f,count); close(fd); return s;
}
void server_join(Server s) { int status; T(waitpid(s.pid,&status,0)==s.pid); T(WIFEXITED(status)&&WEXITSTATUS(status)==0); }
