#include "support.h"
#include <pty.h>
#include <poll.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#include <jansson.h>
typedef struct { pid_t pid; int fd; char *log; size_t len; char dir[64]; } Terminal;
static char normal_binary[PATH_MAX],fixture_binary[PATH_MAX];
static long long milliseconds(void) { struct timespec t; T(clock_gettime(CLOCK_MONOTONIC,&t)==0); return (long long)t.tv_sec*1000+t.tv_nsec/1000000; }
static void pump(Terminal *t,unsigned ms) {
    long long end=milliseconds()+ms;
    do {
        struct pollfd fd={t->fd,POLLIN,0}; int r=poll(&fd,1,10);
        if (r>0&&(fd.revents&POLLIN)) {
            char b[4096]; ssize_t n=read(t->fd,b,sizeof(b));
            if (n>0) { T(t->len+(size_t)n<8u*1024u*1024u); char *p=realloc(t->log,t->len+(size_t)n+1); T(p); t->log=p; memcpy(p+t->len,b,(size_t)n); t->len+=(size_t)n; p[t->len]=0; }
        }
    } while (milliseconds()<end);
}
static Terminal launch(const char *binary,const char *url) {
    Terminal t={0}; strcpy(t.dir,"/tmp/lyrcensor-terminal-XXXXXX"); T(mkdtemp(t.dir));
    struct winsize size={.ws_row=30,.ws_col=100}; t.pid=forkpty(&t.fd,NULL,NULL,&size); T(t.pid>=0);
    if (!t.pid) {
        alarm(20); T(chdir(t.dir)==0); setenv("TERM","xterm-256color",1); setenv("LC_ALL","C.UTF-8",1); unsetenv("OPENROUTER_MODEL");
        if (url) { setenv("LYRCENSOR_TEST_URL",url,1); setenv("OPENROUTER_API_KEY","fixture-only",1); setenv("NO_PROXY","127.0.0.1",1); }
        else { unsetenv("OPENROUTER_API_KEY"); unsetenv("LYRCENSOR_TEST_URL"); }
        execl(binary,binary,(char *)NULL); _exit(127);
    }
    pump(&t,150); return t;
}
static void send_keys(Terminal *t,const char *s) { write_all(t->fd,s,strlen(s)); pump(t,100); }
static void paste(Terminal *t,const char *s) { write_all(t->fd,"\033[200~",6); write_all(t->fd,s,strlen(s)); write_all(t->fd,"\033[201~",6); pump(t,200); }
static char *wait_file(Terminal *t,const char *name,size_t *len) {
    char path[512]; snprintf(path,sizeof(path),"%s/%s",t->dir,name);
    for (unsigned i=0;i<100;i++) { pump(t,20); char *b=read_file(path,len); if (b) return b; }
    fprintf(stderr,"Missing output %s\n%s\n",path,t->log?t->log:""); T(false); return NULL;
}
static void check_file(Terminal *t,const char *name,const char *expected) {
    size_t n; char *b=wait_file(t,name,&n); T(n==strlen(expected)&&!memcmp(b,expected,n)); free(b);
}
static void finish(Terminal *t,int expected) {
    int status=0; bool exited=false;
    for (unsigned i=0;i<100;i++) { pump(t,20); pid_t p=waitpid(t->pid,&status,WNOHANG); if (p==t->pid) { exited=true; break; } T(p>=0); }
    if (!exited) { kill(t->pid,SIGKILL); waitpid(t->pid,&status,0); fprintf(stderr,"Terminal child did not exit.\n%s\n",t->log?t->log:""); }
    T(exited); T(WIFEXITED(status)&&WEXITSTATUS(status)==expected); close(t->fd);
}
static void remove_file(Terminal *t,const char *name) { char path[512]; snprintf(path,sizeof(path),"%s/%s",t->dir,name); T(unlink(path)==0); }
static void cleanup(Terminal *t) { free(t->log); T(rmdir(t->dir)==0); }
static void exit_editor(Terminal *t) { send_keys(t,"\033"); send_keys(t,"\033"); finish(t,0); }
static void test_editor(void) {
    Terminal t=launch(normal_binary,NULL);
    paste(&t,"Ёж  hello-world\r\n\nе\xcc\x81\tabc123\nLAST");
    send_keys(&t,"\033[H\033[3~\033[F\177\rновая\033[A\033[HX");
    struct winsize tiny={.ws_row=8,.ws_col=30}; T(ioctl(t.fd,TIOCSWINSZ,&tiny)==0); pump(&t,100);
    struct winsize normal={.ws_row=30,.ws_col=100}; T(ioctl(t.fd,TIOCSWINSZ,&normal)==0); pump(&t,100);
    /* Escape sequences inside a paste must never become F5/exit commands. */
    paste(&t,"\033[15~ATTACK"); send_keys(&t,"\033OS");
    const char *expected="Ёж  hello-world\r\n\nе\xcc\x81\tabc123\nXAS\nновая";
    check_file(&t,"LYRCENSOR draft.md",expected);
    send_keys(&t,"\033OR"); send_keys(&t,"r"); send_keys(&t,"\033OR"); /* F3 before a run never edits source */
    send_keys(&t,"\033OP"); send_keys(&t,"\033"); /* help and return */
    send_keys(&t,"\033OQ"); send_keys(&t,"typesafe/jev-1.13\033[B5\033[B0.2\033[B0.75\r");
    send_keys(&t,"\033[15~"); /* missing API key leaves source editable */
    send_keys(&t,"\033OS"); check_file(&t,"LYRCENSOR draft (1).md",expected);
    exit_editor(&t); remove_file(&t,"LYRCENSOR draft.md"); remove_file(&t,"LYRCENSOR draft (1).md"); cleanup(&t);
}
static void test_success(void) {
    Fixture f[3]; for (size_t i=0;i<3;i++) f[i]=(Fixture){.status=200,.dynamic=true}; Server s=server_start(f,3);
    Terminal t=launch(fixture_binary,s.url); paste(&t,"Song\nПривет  hello-world!\n"); send_keys(&t,"\033[15~");
    check_file(&t,"Song.md","****\n******  *****-*****!\n"); pump(&t,200);
    T(t.log&&strstr(t.log,"--- LYRCENSOR result begins ---"));
    T(strstr(t.log,"****\r\n******  *****-*****!\r\n"));
    T(strstr(t.log,"Requests completed/started: 3/3; validated answers: 5."));
    T(strstr(t.log,"API-reported cost: $0.000369000")); T(strstr(t.log,"300 input, 0 output"));
    send_keys(&t,"\033OR");
    T(strstr(t.log,"noul = P(YES)")); T(strstr(t.log,"TOKENS (descending probability)"));
    T(strstr(t.log,"line=2 token=1 P(YES)=1 CENSOR")); T(!strstr(t.log,"fixture-only"));
    send_keys(&t,"r"); size_t n; char *report=wait_file(&t,"LYRCENSOR diagnostics.json",&n);
    json_error_t error; json_t *json=json_loadb(report,n,JSON_REJECT_DUPLICATES,&error); T(json); free(report);
    T(json_is_true(json_object_get(json,"classification_complete")));
    T(json_array_size(json_object_get(json,"lines"))==2&&json_array_size(json_object_get(json,"tokens"))==3);
    T(json_number_value(json_object_get(json,"stage2_threshold"))==0.75);
    json_decref(json);
    check_file(&t,"Song.md","****\n******  *****-*****!\n");
    send_keys(&t,"\r"); finish(&t,0); server_join(s); remove_file(&t,"Song.md"); remove_file(&t,"LYRCENSOR diagnostics.json"); cleanup(&t);
}
static void test_cancel_and_failure(void) {
    for (unsigned kind=0;kind<2;kind++) {
        Fixture f={.status=kind?401:200,.body=kind?"{}":NULL,.dynamic=!kind,.delay_ms=kind?0u:1000u}; Server s=server_start(&f,1);
        Terminal t=launch(fixture_binary,s.url); paste(&t,"Keep\nOriginal  ё\ttext"); send_keys(&t,"\033[15~");
        if (!kind) { send_keys(&t,"\033[13~"); send_keys(&t,"\t\033[B\033[C"); send_keys(&t,"\033OR"); send_keys(&t,"\033"); }
        pump(&t,350); send_keys(&t,"\033OS"); check_file(&t,"LYRCENSOR draft.md","Keep\nOriginal  ё\ttext");
        exit_editor(&t); server_join(s); remove_file(&t,"LYRCENSOR draft.md"); cleanup(&t);
    }
}
static void test_signal(void) {
    Terminal t=launch(normal_binary,NULL); paste(&t,"Recovery\r\n\nё  e\xcc\x81"); T(kill(t.pid,SIGTERM)==0);
    check_file(&t,"LYRCENSOR recovery.md","Recovery\r\n\nё  e\xcc\x81"); finish(&t,128+SIGTERM); remove_file(&t,"LYRCENSOR recovery.md"); cleanup(&t);
}
static void test_large_paste(void) {
    Terminal t=launch(normal_binary,NULL); size_t n=128u*1024u;
    char *large=malloc(n+16); T(large); memset(large,'a',n); memcpy(large+n,"ёe\xcc\x81\t界",10); large[n+10]=0;
    paste(&t,large); send_keys(&t,"\033OS"); check_file(&t,"LYRCENSOR draft.md",large);
    exit_editor(&t); remove_file(&t,"LYRCENSOR draft.md"); free(large); cleanup(&t);
}
int main(void) {
    T(realpath("./lyrcensor",normal_binary)); T(realpath("./tests/lyrcensor_fixture",fixture_binary));
    test_editor(); test_success(); test_cancel_and_failure(); test_signal(); test_large_paste();
    puts("PASS: real TUI in PTY, Unicode editing/paste, resize, settings, full Decisions/save/final-display flow, cancel/error recovery, SIGTERM recovery."); return 0;
}
