#include "tui.h"
#include "unicode.h"
#include <curl/curl.h>
#include <locale.h>
#include <langinfo.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
static void signal_handler(int sig) { ly_signal=sig; }
static bool load_file(const char *name,Editor *e,char *error) {
    FILE *f=fopen(name,"rb"); if (!f) { error_set(error,"Cannot open input: %s.",strerror(errno)); return false; }
    char chunk[8192]; bool ok=true; size_t n;
    while ((n=fread(chunk,1,sizeof(chunk),f))) if (!buf_append(&e->text,chunk,n)) { error_set(error,"Input exceeds 16 MiB or memory allocation failed."); ok=false; break; }
    if (ferror(f)) { error_set(error,"Cannot read input file."); ok=false; }
    if (fclose(f)&&ok) { error_set(error,"Cannot close input file."); ok=false; }
    if (ok&&!u_valid_text(e->text.data?e->text.data:"",e->text.len)) { error_set(error,"Input is not valid UTF-8 or contains unsafe terminal controls."); ok=false; }
    return ok;
}
int main(int argc,char **argv) {
    if (argc==2&&!strcmp(argv[1],"--help")) {
        puts("LYRCENSOR - contextual lyrics redaction using OpenRouter Decisions\nUsage: lyrcensor [UTF-8-file]\n\nOPENROUTER_API_KEY is required for processing.\nOPENROUTER_MODEL defaults to typesafe/jev-1.13.\nF1 help, F2 settings, F3 diagnostics, F4 save draft, F5 process, Esc exit/cancel.\nRequires an interactive UTF-8 terminal, at least 48 columns x 12 rows."); return 0;
    }
    if (argc==2&&!strcmp(argv[1],"--version")) { puts("LYRCENSOR 1.0.0"); return 0; }
    if (argc>2) { fprintf(stderr,"Usage: lyrcensor [UTF-8-file]\n"); return 2; }
    if (!setlocale(LC_CTYPE,"") || (strcmp(nl_langinfo(CODESET),"UTF-8")&&strcmp(nl_langinfo(CODESET),"UTF8"))) {
        fprintf(stderr,"A UTF-8 locale is required (for example LC_ALL=C.UTF-8).\n"); return 1;
    }
    if (!isatty(STDIN_FILENO)||!isatty(STDOUT_FILENO)) { fprintf(stderr,"Run LYRCENSOR in an interactive terminal. See --help.\n"); return 1; }
    int dirfd=open(".",O_RDONLY|O_DIRECTORY|O_CLOEXEC); char *cwd=getcwd(NULL,0);
    if (dirfd<0||!cwd) { fprintf(stderr,"Cannot capture launch directory: %s\n",strerror(errno)); if (dirfd>=0) close(dirfd); free(cwd); return 1; }
    Editor editor={0}; Config cfg; config_init(&cfg); char error[LY_ERROR]={0}; int code=0;
    if (argc==2&&!load_file(argv[1],&editor,error)) { fprintf(stderr,"%s\n",error); code=1; goto done; }
    struct sigaction sa={0}; sa.sa_handler=signal_handler; sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,&sa,NULL); sigaction(SIGTERM,&sa,NULL); sigaction(SIGHUP,&sa,NULL);
    sa.sa_handler=SIG_IGN; sigaction(SIGPIPE,&sa,NULL);
    if (curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK) { fprintf(stderr,"Cannot initialize libcurl.\n"); code=1; goto done; }
    Result result={0}; int r=tui_run(&editor,&cfg,dirfd,&result,error); curl_global_cleanup();
    if (r<0) { fprintf(stderr,"%s\n",error); code=1; }
    if (r==1) {
        fprintf(stderr,"\n--- LYRCENSOR result begins ---\n"); fflush(stderr);
        bool printed=fwrite(result.lyrics.data,1,result.lyrics.len,stdout)==result.lyrics.len&&fflush(stdout)==0;
        fprintf(stderr,"\n\n--- LYRCENSOR result ends ---\nSaved: %s/%s\n",cwd,result.filename);
        Progress *p=&result.progress;
        fprintf(stderr,"Lines: %zu/%zu; candidate lines: %zu; tokens evaluated: %zu; redacted: %zu.\nRequests completed/started: %zu/%zu; validated answers: %zu.\n",p->lines_done,p->lines_total,p->candidates,p->tokens_done,p->tokens_marked,p->requests_completed,p->requests_started,p->answers);
        if (p->usage.token_reports) fprintf(stderr,"API-reported tokens: %llu input, %llu output (%zu responses).\n",p->usage.input_tokens,p->usage.output_tokens,p->usage.token_reports);
        else fprintf(stderr,"API-reported token usage: unavailable.\n");
        if (p->usage.cost_reports) fprintf(stderr,"API-reported cost: $%.9f (%zu responses; totals cover reported requests only).\n",p->usage.cost,p->usage.cost_reports);
        else fprintf(stderr,"API-reported cost: unavailable.\n");
        if (!printed) { fprintf(stderr,"Terminal output failed; the complete result is in the saved file.\n"); code=1; }
        if (!ly_signal) tui_result_prompt(result.diagnostics,dirfd,cwd);
    }
    buf_free(&result.lyrics); free(result.filename); diagnostics_free(result.diagnostics);
    if (ly_signal) code=128+ly_signal;
done:
    buf_free(&editor.text); close(dirfd); free(cwd); return code;
}
