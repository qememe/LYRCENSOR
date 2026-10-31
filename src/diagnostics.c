#include "diagnostics.h"
#include "output.h"
#include <jansson.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
Diagnostics *diagnostics_create(const char *s,size_t n,const Config *cfg) {
    Diagnostics *d=calloc(1,sizeof(*d)); if (!d) return NULL;
    d->source=slice(s,n); d->source_len=n; d->config=*cfg;
    if (!d->source || pthread_mutex_init(&d->lock,NULL)) { free(d->source); free(d); return NULL; }
    return d;
}
void diagnostics_free(Diagnostics *d) {
    if (!d) return;
    pthread_mutex_destroy(&d->lock); free(d->source); free(d->records); free(d);
}
bool diagnostics_record(Diagnostics *d,unsigned stage,size_t line,size_t token,size_t start,size_t end,double p) {
    if (!d) return true; /* Optional for library callers; the application always records. */
    if ((stage!=1&&stage!=2)||start>=end||end>d->source_len||!isfinite(p)||p<0||p>1) return false;
    bool ok=true; pthread_mutex_lock(&d->lock);
    if (d->len==d->cap) {
        size_t cap=d->cap?d->cap*2:64;
        DiagnosticRecord *r=cap<=LY_LIMIT/sizeof(*r)?realloc(d->records,cap*sizeof(*r)):NULL;
        if (!r) ok=false;
        else { d->records=r; d->cap=cap; }
    }
    if (ok) d->records[d->len++]=(DiagnosticRecord){stage,line,token,start,end,p,p>=(stage==1?d->config.line_threshold:d->config.token_threshold)};
    pthread_mutex_unlock(&d->lock); return ok;
}
void diagnostics_finish(Diagnostics *d,bool complete) {
    if (!d) return;
    pthread_mutex_lock(&d->lock); d->complete=complete; pthread_mutex_unlock(&d->lock);
}
static int compare(const void *a,const void *b) {
    const DiagnosticRecord *x=a,*y=b;
    if (x->stage!=y->stage) return x->stage<y->stage?-1:1;
    if (x->stage==2 && x->probability!=y->probability) return x->probability>y->probability?-1:1;
    if (x->line_id!=y->line_id) return x->line_id<y->line_id?-1:1;
    return x->token_id<y->token_id?-1:x->token_id>y->token_id?1:0;
}
bool diagnostics_snapshot(Diagnostics *d,unsigned stage,DiagnosticRecord **out,size_t *count,bool *complete) {
    *out=NULL; *count=0; *complete=false; if (!d) return true;
    pthread_mutex_lock(&d->lock);
    size_t n=0; for (size_t i=0;i<d->len;i++) if (!stage||d->records[i].stage==stage) n++;
    DiagnosticRecord *r=n?malloc(n*sizeof(*r)):NULL;
    if (n&&!r) { pthread_mutex_unlock(&d->lock); return false; }
    size_t j=0; for (size_t i=0;i<d->len;i++) if (!stage||d->records[i].stage==stage) r[j++]=d->records[i];
    *complete=d->complete; pthread_mutex_unlock(&d->lock);
    if (n>1) qsort(r,n,sizeof(*r),compare);
    *out=r; *count=n; return true;
}
bool diagnostics_format(const Diagnostics *d,const DiagnosticRecord *r,Buffer *out) {
    char b[256]; int n;
    if (r->stage==2) n=snprintf(b,sizeof(b),"line=%zu token=%zu P(YES)=%.17g %s bytes=[%zu,%zu) ",r->line_id+1,r->token_id,r->probability,r->selected?"CENSOR":"KEEP",r->start,r->end);
    else n=snprintf(b,sizeof(b),"line=%zu id=line_%zu P(YES)=%.17g %s bytes=[%zu,%zu) ",r->line_id+1,r->line_id,r->probability,r->selected?"REVIEW":"SKIP",r->start,r->end);
    if (n<0||(size_t)n>=sizeof(b)||!buf_append(out,b,(size_t)n)) return false;
    json_t *s=json_stringn(d->source+r->start,r->end-r->start);
    char *quoted=s?json_dumps(s,JSON_ENCODE_ANY|JSON_COMPACT):NULL; json_decref(s);
    bool ok=quoted&&buf_append(out,quoted,strlen(quoted)); free(quoted); return ok;
}
bool diagnostics_print(Diagnostics *d,FILE *f,char *error) {
    DiagnosticRecord *records=NULL; size_t count=0; bool complete;
    if (!d) { error_set(error,"No classification diagnostics yet; press F5 to process lyrics."); return false; }
    if (!diagnostics_snapshot(d,0,&records,&count,&complete)) goto fail;
    fprintf(f,"\n--- LYRCENSOR diagnostics (%s) ---\nModel: %s\nStage 1 threshold: %.17g | Stage 2 threshold: %.17g\nnoul = P(YES); decision uses probability >= threshold.\nLine numbers are 1-based; token IDs are 0-based within their line.\n",complete?"complete":"partial",d->config.model,d->config.line_threshold,d->config.token_threshold);
    /* Print tokens first so a zero-redaction run immediately exposes its highest score. */
    for (unsigned pass=2;pass>0;pass--) {
        fprintf(f,"%s\n",pass==2?"TOKENS (descending probability):":"LINES (source order):");
        for (size_t i=0;i<count;i++) {
            if (records[i].stage!=pass) continue;
            Buffer b={0}; bool ok=diagnostics_format(d,&records[i],&b);
            if (!ok) { buf_free(&b); goto fail; }
            fprintf(f,"%s\n",b.data); buf_free(&b);
        }
    }
    fprintf(f,"--- End diagnostics ---\n"); free(records);
    if (!ferror(f)) return true;
    error_set(error,"Cannot write diagnostics to terminal."); return false;
fail: free(records); error_set(error,"Cannot allocate diagnostic display."); return false;
}
bool diagnostics_save(Diagnostics *d,int dirfd,char **filename,char *error) {
    *filename=NULL;
    if (!d) { error_set(error,"No classification diagnostics to save."); return false; }
    DiagnosticRecord *records=NULL; size_t count=0; bool complete;
    if (!diagnostics_snapshot(d,0,&records,&count,&complete)) goto fail;
    json_t *lines=json_array(),*tokens=json_array();
    json_t *root=json_pack("{s:s,s:s,s:b,s:s,s:f,s:f,s:o,s:o}",
        "format","LYRCENSOR diagnostics v1","model",d->config.model,"classification_complete",complete,
        "noul_semantics","P(YES); select if noul >= threshold", "stage1_threshold",d->config.line_threshold,
        "stage2_threshold",d->config.token_threshold,"lines",lines,"tokens",tokens);
    if (!root) goto fail;
    for (size_t i=0;i<count;i++) {
        DiagnosticRecord r=records[i]; char id[80];
        if (r.stage==1) snprintf(id,sizeof(id),"line_%zu",r.line_id);
        else snprintf(id,sizeof(id),"line_%zu_token_%zu",r.line_id,r.token_id);
        json_t *entry=json_pack("{s:s,s:I,s:I,s:I,s:I,s:o,s:f,s:f,s:s,s:b}",
            "question_id",id,"line_id",(json_int_t)r.line_id,"line_number",(json_int_t)(r.line_id+1),
            "byte_start",(json_int_t)r.start,"byte_end",(json_int_t)r.end,
            "original",json_stringn(d->source+r.start,r.end-r.start),"noul",r.probability,
            "threshold",r.stage==1?d->config.line_threshold:d->config.token_threshold,
            "decision",r.stage==1?(r.selected?"review":"skip"):(r.selected?"censor":"keep"),"span_verified",1);
        if (!entry) { json_decref(root); goto fail; }
        if (r.stage==2 && json_object_set_new(entry,"token_id",json_integer((json_int_t)r.token_id))<0) { json_decref(entry); json_decref(root); goto fail; }
        if (json_array_append_new(r.stage==1?lines:tokens,entry)<0) { json_decref(root); goto fail; }
    }
    char *s=json_dumps(root,JSON_INDENT(2)); json_decref(root); free(records); records=NULL;
    if (!s) goto fail;
    Buffer b={0}; bool ok=buf_append(&b,s,strlen(s))&&buf_append(&b,"\n",1); free(s);
    if (ok) ok=output_save_report(dirfd,&b,filename,error);
    else error_set(error,"Diagnostic report exceeds memory/16 MiB limit.");
    buf_free(&b); return ok;
fail: free(records); error_set(error,"Cannot allocate diagnostic report."); return false;
}
