#include "tui.h"
#include "output.h"
#include "unicode.h"
#include <curses.h>
#include <pthread.h>
#include <stdatomic.h>
#include <poll.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <limits.h>
#include <time.h>
#include <termios.h>

enum { K_HELP=400,K_SETTINGS,K_PROCESS,K_SAVE,K_ESC,K_EOF,K_PASTE,K_BAD,K_DIAGNOSTICS };
typedef struct { int key; char text[8]; size_t n; } InputEvent;
typedef struct {
    unsigned char data[8192]; size_t pos,len;
    bool paste, overflow; Buffer pasted; char tail[6]; size_t tail_len;
} Input;
/* Raw byte input is deliberate: curses keypad decoding must never run during paste. */
static int input_byte(Input *in,int timeout) {
    if (in->pos<in->len) return in->data[in->pos++];
    struct pollfd fd={STDIN_FILENO,POLLIN,0}; int r=poll(&fd,1,timeout);
    if (r<=0) return -1;
    ssize_t n=read(STDIN_FILENO,in->data,sizeof(in->data));
    if (n<=0) return n<0&&(errno==EINTR||errno==EAGAIN)?-1:-2;
    in->len=(size_t)n; in->pos=1; return in->data[0];
}
static InputEvent input_event(Input *in) {
    InputEvent e={0}; int c=input_byte(in,50);
    if (c==-2) { e.key=K_EOF; return e; }
    if (c<0) return e;
    if (in->paste) {
        /* Process bounded chunks so the main thread can redraw/cancel on signals. */
        for (size_t work=0;work<8192;work++) {
            if (in->tail_len==sizeof(in->tail)) {
                if (!in->overflow&&!buf_append(&in->pasted,in->tail,1)) in->overflow=true;
                memmove(in->tail,in->tail+1,sizeof(in->tail)-1); in->tail_len--;
            }
            in->tail[in->tail_len++]=(char)c;
            if (in->tail_len==6&&!memcmp(in->tail,"\033[201~",6)) {
                in->paste=false; in->tail_len=0; e.key=in->overflow?K_BAD:K_PASTE; return e;
            }
            c=input_byte(in,0); if (c<0) { if (c==-2) e.key=K_EOF; return e; }
        }
        /* The byte already read for the next iteration remains in the queue. */
        in->pos--; return e;
    }
    if (c==27) {
        struct { const char *s; int key; } const seq[]={
            {"[A",ED_UP},{"[B",ED_DOWN},{"[C",ED_RIGHT},{"[D",ED_LEFT},
            {"OA",ED_UP},{"OB",ED_DOWN},{"OC",ED_RIGHT},{"OD",ED_LEFT},
            {"[H",ED_HOME},{"[F",ED_END},{"OH",ED_HOME},{"OF",ED_END},
            {"[1~",ED_HOME},{"[4~",ED_END},{"[7~",ED_HOME},{"[8~",ED_END},
            {"[3~",ED_DELETE},{"[5~",ED_PGUP},{"[6~",ED_PGDN},
            {"OP",K_HELP},{"OQ",K_SETTINGS},{"OR",K_DIAGNOSTICS},{"OS",K_SAVE},
            {"[11~",K_HELP},{"[12~",K_SETTINGS},{"[13~",K_DIAGNOSTICS},{"[14~",K_SAVE},{"[15~",K_PROCESS},
            {"[200~",K_PASTE}};
        char s[32]={0}; size_t n=0;
        while (n<sizeof(s)-1) {
            int v=input_byte(in,40);
            if (v<0) { e.key=n?0:K_ESC; return e; }
            s[n++]=(char)v; bool prefix=false;
            for (size_t j=0;j<sizeof(seq)/sizeof(seq[0]);j++) {
                size_t k=strlen(seq[j].s);
                if (n<=k&&!memcmp(s,seq[j].s,n)) {
                    if (n==k) {
                        if (seq[j].key==K_PASTE) { in->paste=true; in->overflow=false; in->tail_len=0; buf_free(&in->pasted); return e; }
                        e.key=seq[j].key; return e;
                    }
                    prefix=true;
                }
            }
            if (!prefix) {
                /* Consume an unknown CSI sequence through its final byte. */
                if (s[0]=='[') while (n<sizeof(s)-1 && !(s[n-1]>=0x40&&s[n-1]<=0x7e&&n>1)) {
                    v=input_byte(in,40); if (v<0) break; s[n++]=(char)v;
                }
                return e;
            }
        }
        return e;
    }
    if (c==3) { ly_signal=SIGINT; return e; }
    if (c==127||c==8) { e.key=ED_BACK; return e; }
    if (c=='\r'||c=='\n') { e.key=ED_ENTER; return e; }
    if (c<32&&c!='\t') return e;
    e.text[0]=(char)c; e.n=1;
    size_t expected=c<128?1:(c>=0xc2&&c<=0xdf?2:(c>=0xe0&&c<=0xef?3:(c>=0xf0&&c<=0xf4?4:0)));
    if (!expected) { e.key=K_BAD; e.n=0; return e; }
    while (e.n<expected) {
        c=input_byte(in,100);
        if (c<0) { e.key=K_BAD; e.n=0; return e; }
        if ((c&0xc0)!=0x80) { in->pos--; e.key=K_BAD; e.n=0; return e; }
        e.text[e.n++]=(char)c;
    }
    if (!u_valid_text(e.text,e.n)) { e.key=K_BAD; e.n=0; }
    return e;
}
static void put(int row,int col,const char *text) {
    int rows,cols; getmaxyx(stdscr,rows,cols);
    if (row<0||row>=rows||col<0||col>=cols) return;
    int x=col; size_t n=strlen(text);
    for (size_t p=0;p<n;) {
        int32_t cp; size_t k=u_decode(text+p,n-p,&cp); if (!k) break;
        size_t q=u_next(text,n,p),w=u_width(text,p,q,(size_t)x);
        if (x+(int)w>cols-1) break;
        if (cp>=32&&cp!=127) mvaddnstr(row,x,text+p,(int)(q-p));
        x+=(int)w; p=q;
    }
}
static void rule(int y) {
    int rows,cols; getmaxyx(stdscr,rows,cols); if (y<0||y>=rows||cols<2) return;
    mvaddch(y,0,'+'); for (int x=1;x<cols-1;x++) mvaddch(y,x,'-'); mvaddch(y,cols-1,'+');
}
static void frame(const Config *cfg,const char *mode,const char *message) {
    int rows,cols; getmaxyx(stdscr,rows,cols); erase();
    if (rows<12||cols<48) { put(0,0,"LYRCENSOR: resize terminal to at least 48x12."); put(2,0,"Esc exits / cancels. Ctrl+C quits safely."); return; }
    rule(0); rule(2); rule(4); rule(rows-4); rule(rows-1);
    for (int y=1;y<rows-1;y++) if (y!=2&&y!=4&&y!=rows-4) { mvaddch(y,0,'|'); mvaddch(y,cols-1,'|'); }
    attron(A_BOLD); put(1,2,"LYRCENSOR"); put(1,cols>72?cols-32:16,mode); attroff(A_BOLD);
    char b[512]; snprintf(b,sizeof(b),"MODEL: %s",cfg->model); put(3,2,b);
    put(rows-3,2,message);
    put(rows-2,2,"F1 Help | F2 Settings | F3 Diagnostics | F5 Process | Esc");
}
static void draw_editor(Editor *e,int height,int width) {
    const char *s=e->text.data?e->text.data:""; size_t n=e->text.len;
    size_t current=editor_start(e);
    /* top is a byte line start; recalculate after arbitrary insert/delete. */
    if (e->top>n) e->top=n;
    if (e->top && e->top<n && s[e->top]=='\n' && s[e->top-1]=='\r') e->top--;
    while (e->top&&s[e->top-1]!='\n'&&s[e->top-1]!='\r') e->top--;
    if (current<e->top) e->top=current;
    size_t cursor_row=0,p=e->top;
    while (p<current) { Line l=line_at(s,n,p); if (l.next<=p) break; p=l.next; cursor_row++; }
    while (cursor_row>=(size_t)height) { e->top=line_at(s,n,e->top).next; cursor_row--; }
    size_t column=editor_column(e);
    if (column<e->left) e->left=column;
    if (column>=e->left+(size_t)width) e->left=column-(size_t)width+1;
    p=e->top;
    for (int row=0;row<height;row++) {
        Line l=line_at(s,n,p); size_t col=0;
        for (size_t a=l.start;a<l.end;) {
            size_t b=u_next(s,l.end,a),w=u_width(s,a,b,col);
            if (col>=e->left+(size_t)width) break;
            if (col>=e->left && col+w<=e->left+(size_t)width && s[a]!='\t') {
                if (w) mvaddnstr(5+row,2+(int)(col-e->left),s+a,(int)(b-a));
            }
            col+=w; a=b;
        }
        if (l.next==l.end) break;
        p=l.next;
    }
    curs_set(1); move(5+(int)cursor_row,2+(int)(column-e->left));
}
typedef struct {
    pthread_mutex_t lock; pthread_t thread; atomic_bool cancel,done;
    char *source; size_t len; Config config; int dirfd;
    Progress progress; Buffer result; char *filename; bool success; char error[LY_ERROR];
    Diagnostics *diagnostics;
} Job;
static void publish(void *ctx,const Progress *p) { Job *j=ctx; pthread_mutex_lock(&j->lock); j->progress=*p; pthread_mutex_unlock(&j->lock); }
static void *worker(void *ctx) {
    Job *j=ctx; Progress p={0};
    ApiClient a={.config=&j->config,.key=getenv("OPENROUTER_API_KEY"),.cancel=&j->cancel,.progress=&p,.notify=publish,.notify_ctx=j};
    if (api_init(&a,j->error)) {
        j->success=classifier_run(j->source,j->len,&j->config,j->diagnostics,&j->cancel,&p,publish,j,api_decide,&a,&j->result,j->error);
        api_cleanup(&a);
    }
    if (j->success&&(atomic_load(&j->cancel)||ly_signal)) { j->success=false; error_set(j->error,"Processing cancelled before saving."); }
    if (j->success) {
        progress_notify(&p,publish,j,"Writing and syncing result in launch directory...");
        j->success=output_save(j->dirfd,j->source,j->len,&j->result,&j->filename,j->error);
    }
    publish(j,&p); atomic_store_explicit(&j->done,true,memory_order_release); return NULL;
}
static void draw_progress(const Progress *p) {
    int rows,cols; getmaxyx(stdscr,rows,cols); if (rows<12||cols<48) return;
    if (!p->stage) { put(5,3,"Preparing immutable processing snapshot..."); curs_set(0); return; }
    char b[256]; snprintf(b,sizeof(b),"STAGE %u/2 : %s",p->stage,p->stage==1?"SCANNING LINES":"CLASSIFYING WORDS"); put(5,3,b);
    size_t done=p->stage==1?p->lines_done:p->candidates_done, total=p->stage==1?p->lines_total:p->candidates;
    unsigned percent=total?(unsigned)(100*done/total):(p->stage==2?100:0);
    if (rows<18) {
        snprintf(b,sizeof(b),"%u%% | Completed: %zu/%zu",percent,done,total); put(6,3,b);
        snprintf(b,sizeof(b),"Words: %zu | Mask: %zu | Requests: %zu",p->tokens_done,p->tokens_marked,p->requests_completed); put(7,3,b);
        if (rows>12) put(8,3,p->activity);
        curs_set(0); return;
    }
    int w=cols-12; if (w>64) w=64;
    mvaddch(7,3,'['); for (int x=0;x<w;x++) addch((unsigned)x*100<(unsigned)w*percent?'=':'.'); addch(']');
    snprintf(b,sizeof(b),"%u%% | Lines: %zu/%zu | Candidates: %zu/%zu",percent,p->lines_done,p->lines_total,p->candidates_done,p->candidates); put(8,3,b);
    snprintf(b,sizeof(b),"Tokens evaluated: %zu | Marked: %zu",p->tokens_done,p->tokens_marked); put(9,3,b);
    if (rows>16) { snprintf(b,sizeof(b),"Requests complete/started: %zu/%zu | Answers: %zu",p->requests_completed,p->requests_started,p->answers); put(11,3,b); put(13,3,p->activity); }
    if (rows>20) {
        snprintf(b,sizeof(b),"Reported tokens: %llu in / %llu out (%zu reports)",p->usage.input_tokens,p->usage.output_tokens,p->usage.token_reports); put(15,3,b);
        if (p->usage.cost_reports) snprintf(b,sizeof(b),"API-reported cost: $%.9f (%zu reports)",p->usage.cost,p->usage.cost_reports);
        else snprintf(b,sizeof(b),"API-reported cost: unavailable");
        put(16,3,b);
    }
    curs_set(0);
}
static void settings_draw(const char fields[4][256],int selected,const char *msg) {
    int rows,cols; getmaxyx(stdscr,rows,cols); if (rows<12||cols<48) return;
    const char *labels[]={"Model ID (Decisions API)","Network timeout seconds","Stage 1 review threshold","Stage 2 redact threshold"};
    int visible=(rows-8)/2; if (visible>4) visible=4;
    int first=selected>=visible?selected-visible+1:0;
    for (int j=0;j<visible&&first+j<4;j++) {
        int i=first+j;
        char b[512]; snprintf(b,sizeof(b),"%c %s: %s",selected==i?'>':' ',labels[i],fields[i]);
        if (selected==i) attron(A_REVERSE);
        put(5+j*2,3,b);
        if (selected==i) attroff(A_REVERSE);
    }
    if (rows>19) { put(14,3,config_key_valid(getenv("OPENROUTER_API_KEY"))?"API key: configured (environment)":"API key: missing or invalid (environment)"); put(16,3,"Up/Down choose | Type replaces field | Enter saves"); put(17,3,"Esc returns without changes. Settings last this session."); }
    put(rows-3,2,msg); curs_set(0);
}
static bool settings_apply(char f[4][256],Config *cfg,char *error) {
    Config next=*cfg; memcpy(next.model,f[0],256);
    char *end; errno=0; next.timeout=strtol(f[1],&end,10);
    if (errno||end==f[1]||*end) { error_set(error,"Timeout must be an integer."); return false; }
    errno=0; next.line_threshold=strtod(f[2],&end);
    if (errno||end==f[2]||*end) { error_set(error,"Review threshold must be a number, e.g. 0.20."); return false; }
    errno=0; next.token_threshold=strtod(f[3],&end);
    if (errno||end==f[3]||*end) { error_set(error,"Redact threshold must be a number, e.g. 0.75."); return false; }
    if (!config_valid(&next,error)) return false;
    *cfg=next; return true;
}
static void draw_diagnostics(Diagnostics *d,const Config *active,unsigned stage,size_t *top,size_t left) {
    int rows,cols; getmaxyx(stdscr,rows,cols); curs_set(0);
    if (rows<12||cols<48) return;
    const Config *run=d?&d->config:active; char b[256];
    snprintf(b,sizeof(b),"S1>=%.17g S2>=%.17g",run->line_threshold,run->token_threshold); put(5,2,b);
    put(6,2,"noul=P(YES); >= selects. Tab: lines/tokens");
    DiagnosticRecord *records=NULL; size_t count=0; bool complete;
    if (!diagnostics_snapshot(d,stage,&records,&count,&complete)) { put(7,2,"Cannot allocate diagnostic view."); return; }
    int first=rows>15?9:7, height=rows-4-first;
    if (*top>=count) *top=count?count-1:0;
    if (rows>15) {
        snprintf(b,sizeof(b),"%s | %zu %s | %s snapshot",stage==2?"Descending probability":"Source order",count,stage==2?"tokens":"lines",complete?"complete":"partial"); put(7,2,b);
        if (stage==2&&count) snprintf(b,sizeof(b),"Highest token P(YES)=%.17g; S2=%.6g",records[0].probability,run->token_threshold);
        else snprintf(b,sizeof(b),"Current settings: S1=%.6g S2=%.6g",active->line_threshold,active->token_threshold);
        put(8,2,b);
    }
    if (!count) put(first,2,d?"No validated decisions for this stage yet.":"No run yet. Esc, then F5 to classify.");
    for (int i=0;i<height&&*top+(size_t)i<count;i++) {
        Buffer text={0};
        if (diagnostics_format(d,&records[*top+(size_t)i],&text)) {
            size_t p=0,col=0;
            while (p<text.len&&col<left) { size_t next=u_next(text.data,text.len,p); col+=u_width(text.data,p,next,col); p=next; }
            put(first+i,2,text.data+p);
        } else put(first+i,2,"Cannot format decision.");
        buf_free(&text);
    }
    free(records); put(rows-2,2,"Up/Dn/Pg: scroll | Left/Right | R: report | F3/Esc: back");
}
int tui_run(Editor *editor,Config *cfg,int dirfd,Result *result,char *error) {
    if (!initscr()) { error_set(error,"Cannot initialize terminal UI."); return -1; }
    raw(); noecho(); keypad(stdscr,FALSE); intrflush(stdscr,FALSE);
    /* Enable emulator-native bracketed paste, no mouse capture or clipboard hooks. */
    fputs("\033[?2004h",stdout); fflush(stdout);
    Input input={0}; Job job={0}; bool running=false,help=false,settings=false,replace=false,confirm_exit=false,redraw=true;
    Progress displayed={0};
    Diagnostics *last=NULL; bool diagnostic_view=false; unsigned diagnostic_stage=2;
    size_t diagnostic_top=0,diagnostic_left=0;
    char status[LY_ERROR]="Paste lyrics, edit, then press F5. F4 saves an uncensored draft.";
    char fields[4][256]={{0}}; int selected=0,answer=0;
    while (!ly_signal) {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws)==0&&ws.ws_row&&ws.ws_col&&(LINES!=ws.ws_row||COLS!=ws.ws_col)) { resizeterm(ws.ws_row,ws.ws_col); redraw=true; }
        if (running&&atomic_load_explicit(&job.done,memory_order_acquire)) {
            pthread_join(job.thread,NULL); pthread_mutex_destroy(&job.lock); running=false; job.source=NULL;
            if (job.success) {
                result->lyrics=job.result; result->filename=job.filename; result->progress=job.progress;
                result->diagnostics=job.diagnostics; job.diagnostics=NULL;
                job.result=(Buffer){0}; job.filename=NULL; answer=1; break;
            }
            snprintf(status,sizeof(status),"%s",job.error); buf_free(&job.result); free(job.filename); job.filename=NULL;
            diagnostics_free(last); last=job.diagnostics; job.diagnostics=NULL;
            redraw=true;
        }
        int rows,cols; getmaxyx(stdscr,rows,cols);
        if (running) {
            Progress p; pthread_mutex_lock(&job.lock); p=job.progress; pthread_mutex_unlock(&job.lock);
            if (p.revision!=displayed.revision) { displayed=p; redraw=true; }
        }
        if (redraw) {
        frame(cfg,diagnostic_view?"DIAGNOSTICS":(running?"PROCESSING":(settings?"SETTINGS":(help?"HELP":(config_key_valid(getenv("OPENROUTER_API_KEY"))?"OPENROUTER READY":"API KEY MISSING")))),status);
        if (diagnostic_view) draw_diagnostics(running?job.diagnostics:last,cfg,diagnostic_stage,&diagnostic_top,diagnostic_left);
        else if (running) {
            draw_progress(&displayed); if (rows>=12&&cols>=48) put(rows-2,2,"F3: diagnostics | Esc: cancel | Editor locked");
        } else if (settings) settings_draw((const char (*)[256])fields,selected,status);
        else if (help) {
            const char *lines[]={"Paste: terminal shortcut (usually Ctrl+Shift+V)","Arrows/Home/End/Page keys: move; Tab inserts a tab", "Backspace/Delete: remove grapheme; Enter: new line", "F2: Decisions model, timeout, probability thresholds", "F3: probabilities; R there saves diagnostic report", "F4: save draft; F5: classify immutable source snapshot", "Esc during processing: cancel and keep source", "Final text returns to normal terminal scrollback", "F3 at final prompt: diagnostics; R: JSON report", "No credentials are included in diagnostics. Esc: back"};
            for (size_t i=0;i<sizeof(lines)/sizeof(lines[0])&&5+(int)i<rows-4;i++) put(5+(int)i,3,lines[i]);
            curs_set(0);
        } else if (rows>=12&&cols>=48) draw_editor(editor,rows-9,cols-4);
        refresh(); redraw=false;
        }
        InputEvent ev=input_event(&input);
        if (ev.key||ev.n) redraw=true;
        if (input.paste) {
            if (strcmp(status,"Receiving bracketed paste... (commands disabled)")) { snprintf(status,sizeof(status),"Receiving bracketed paste... (commands disabled)"); redraw=true; }
            continue;
        }
        if (ev.key==K_EOF) break;
        if (ev.key==K_PASTE) {
            if (!running&&!help&&!settings&&!diagnostic_view) {
                if (!editor_insert(editor,input.pasted.data?input.pasted.data:"",input.pasted.len)) snprintf(status,sizeof(status),"Paste rejected: invalid UTF-8, unsafe controls, memory or 16 MiB limit. Source kept.");
                else snprintf(status,sizeof(status),"Pasted %zu bytes unchanged.",input.pasted.len);
            }
            buf_free(&input.pasted); confirm_exit=false; continue;
        }
        if (ev.key==K_BAD) { buf_free(&input.pasted); snprintf(status,sizeof(status),"Input rejected: invalid UTF-8, unsafe controls, memory or size limit."); continue; }
        if (ev.key==K_DIAGNOSTICS&&!settings&&!help) { diagnostic_view=!diagnostic_view; diagnostic_top=diagnostic_left=0; continue; }
        if (diagnostic_view) {
            if (ev.key==K_ESC) diagnostic_view=false;
            else if (ev.key==ED_UP&&diagnostic_top) diagnostic_top--;
            else if (ev.key==ED_DOWN) diagnostic_top++;
            else if (ev.key==ED_PGDN) diagnostic_top+=rows>13?(size_t)(rows-13):1;
            else if (ev.key==ED_PGUP) { size_t page=rows>13?(size_t)(rows-13):1; diagnostic_top=diagnostic_top>page?diagnostic_top-page:0; }
            else if (ev.key==ED_HOME) diagnostic_top=diagnostic_left=0;
            else if (ev.key==ED_RIGHT&&diagnostic_left<LY_LIMIT) diagnostic_left+=8;
            else if (ev.key==ED_LEFT) diagnostic_left=diagnostic_left>=8?diagnostic_left-8:0;
            else if (ev.text[0]=='\t') { diagnostic_stage=diagnostic_stage==2?1:2; diagnostic_top=diagnostic_left=0; }
            else if (ev.text[0]=='r'||ev.text[0]=='R') {
                char *name=NULL;
                if (diagnostics_save(running?job.diagnostics:last,dirfd,&name,status)) { snprintf(status,sizeof(status),"Diagnostic report saved: %s",name); free(name); }
            }
            continue;
        }
        if (running) { if (ev.key==K_ESC) { atomic_store(&job.cancel,true); snprintf(status,sizeof(status),"Cancelling request; source will return to editor..."); } continue; }
        if (settings) {
            if (ev.key==K_ESC) { settings=false; snprintf(status,sizeof(status),"Settings unchanged."); }
            else if (ev.key==ED_UP||ev.key==ED_DOWN||ev.text[0]=='\t') { selected=(selected+(ev.key==ED_UP?3:1))%4; replace=true; }
            else if (ev.key==ED_ENTER) { if (settings_apply(fields,cfg,status)) { settings=false; snprintf(status,sizeof(status),"Settings saved for this session."); } }
            else if (ev.key==ED_BACK||ev.key==ED_DELETE) { size_t n=strlen(fields[selected]); if (n) fields[selected][n-1]=0; replace=false; }
            else if (ev.n) {
                bool ascii=true; for (size_t i=0;i<ev.n;i++) if ((unsigned char)ev.text[i]<32||(unsigned char)ev.text[i]>126) ascii=false;
                if (ascii) { if (replace) fields[selected][0]=0; size_t n=strlen(fields[selected]); if (n+ev.n<256) { memcpy(fields[selected]+n,ev.text,ev.n); fields[selected][n+ev.n]=0; } replace=false; }
            }
            continue;
        }
        if (help) { if (ev.key==K_ESC||ev.key==K_HELP) help=false; continue; }
        if (ev.key==K_ESC) {
            if (!editor->text.len||confirm_exit) break;
            confirm_exit=true; snprintf(status,sizeof(status),"Exit without processing? Press Esc again, or any editing key to continue. F4 saves draft."); continue;
        }
        if (ev.key||ev.n) confirm_exit=false;
        if (ev.key==K_HELP) help=true;
        else if (ev.key==K_SETTINGS) {
            settings=true; selected=0; replace=true;
            snprintf(fields[0],256,"%s",cfg->model); snprintf(fields[1],256,"%ld",cfg->timeout);
            snprintf(fields[2],256,"%.6g",cfg->line_threshold); snprintf(fields[3],256,"%.6g",cfg->token_threshold);
            snprintf(status,sizeof(status),"Choose field; type replaces it. Enter saves; Esc cancels.");
        } else if (ev.key==K_SAVE) {
            char *name=NULL;
            if (output_save(dirfd,"LYRCENSOR draft",15,&editor->text,&name,status)) { snprintf(status,sizeof(status),"Uncensored draft saved: %s",name); free(name); }
        } else if (ev.key==K_PROCESS) {
            if (!editor->text.len) { snprintf(status,sizeof(status),"Input is empty; paste or type lyrics first."); continue; }
            if (!config_key_valid(getenv("OPENROUTER_API_KEY"))) { snprintf(status,sizeof(status),"Set a valid OPENROUTER_API_KEY in the environment and restart. Source is still editable."); continue; }
            if (!config_valid(cfg,status)) continue;
            memset(&job,0,sizeof(job)); job.diagnostics=diagnostics_create(editor->text.data,editor->text.len,cfg);
            if (!job.diagnostics) { snprintf(status,sizeof(status),"Cannot allocate processing snapshot; source kept."); continue; }
            job.source=job.diagnostics->source;
            job.len=editor->text.len; job.config=*cfg; job.dirfd=dirfd;
            atomic_init(&job.cancel,false); atomic_init(&job.done,false);
            int rc=pthread_mutex_init(&job.lock,NULL);
            if (!rc) { rc=pthread_create(&job.thread,NULL,worker,&job); if (rc) pthread_mutex_destroy(&job.lock); }
            if (rc) { diagnostics_free(job.diagnostics); job.diagnostics=NULL; job.source=NULL; snprintf(status,sizeof(status),"Cannot start worker: %s",strerror(rc)); continue; }
            running=true; snprintf(status,sizeof(status),"Processing snapshot. Esc cancels; editor locked.");
        } else if (ev.n) {
            if (!editor_insert(editor,ev.text,ev.n)) snprintf(status,sizeof(status),"Insertion failed: memory or 16 MiB limit; existing text kept.");
        } else if (ev.key==ED_ENTER) {
            if (!editor_insert(editor,"\n",1)) snprintf(status,sizeof(status),"Newline insertion failed: memory or 16 MiB limit; existing text kept.");
        } else if (ev.key>=ED_LEFT&&ev.key<=ED_DELETE) editor_key(editor,ev.key,rows>9?(size_t)(rows-9):1);
    }
    if (running) {
        atomic_store(&job.cancel,true); pthread_join(job.thread,NULL); pthread_mutex_destroy(&job.lock);
        /* A completed write remains a success even if quit arrived during fsync. */
        if (job.success) { result->lyrics=job.result; result->filename=job.filename; result->progress=job.progress; result->diagnostics=job.diagnostics; answer=1; }
        else { buf_free(&job.result); free(job.filename); diagnostics_free(job.diagnostics); }
    }
    diagnostics_free(last);
    buf_free(&input.pasted);
    fputs("\033[?2004l",stdout); fflush(stdout); endwin();
    if (ly_signal&&editor->text.len&&!answer) {
        char *name=NULL;
        if (output_save(dirfd,"LYRCENSOR recovery",18,&editor->text,&name,error)) { fprintf(stderr,"\nSource recovered to: %s\n",name); free(name); }
        else fprintf(stderr,"\nCould not save source recovery: %s\n",error);
    }
    return answer;
}
void tui_result_prompt(Diagnostics *diagnostics,int dirfd,const char *cwd) {
    struct termios saved,mode;
    if (tcgetattr(STDIN_FILENO,&saved)<0) { fprintf(stderr,"Press Enter to exit.\n"); char b[32]; if (!fgets(b,sizeof(b),stdin)) clearerr(stdin); return; }
    mode=saved; mode.c_lflag&=(tcflag_t)~(ICANON|ECHO); mode.c_cc[VMIN]=1; mode.c_cc[VTIME]=0;
    if (tcsetattr(STDIN_FILENO,TCSANOW,&mode)<0) { fprintf(stderr,"Press Enter to exit.\n"); char b[32]; if (!fgets(b,sizeof(b),stdin)) clearerr(stdin); return; }
    Input input={0}; char error[LY_ERROR];
    fprintf(stderr,"Select lyrics and copy normally. F3: diagnostics | R: save report | Enter: exit\n");
    while (!ly_signal) {
        InputEvent ev=input_event(&input);
        if (input.paste) continue;
        if (ev.key==K_EOF||ev.key==ED_ENTER) break;
        if (ev.key==K_DIAGNOSTICS) {
            if (!diagnostics_print(diagnostics,stderr,error)) fprintf(stderr,"%s\n",error);
            fprintf(stderr,"F3: diagnostics | R: save report | Enter: exit\n");
        } else if (ev.text[0]=='r'||ev.text[0]=='R') {
            char *filename=NULL;
            if (diagnostics_save(diagnostics,dirfd,&filename,error)) { fprintf(stderr,"Diagnostic report: %s/%s\n",cwd,filename); free(filename); }
            else fprintf(stderr,"%s\n",error);
        }
        if (ev.key==K_PASTE||ev.key==K_BAD) buf_free(&input.pasted);
    }
    buf_free(&input.pasted); (void)tcsetattr(STDIN_FILENO,TCSANOW,&saved);
}
