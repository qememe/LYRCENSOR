#include "classifier.h"
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while (0)
static const char input[]="skip abc\r\nfoo  ё-моё can't abc123 e\xcc\x81\nsame same same";
static const double line_prob[]={0.199999999999,0.20,0.9};
static const double word_prob[][5]={{0},{0.749999999999,0.75,0.750000000001,0,1},{0.99,0,0.75}};
static bool decide(void *ctx,const json_t *request,double *p,char *error) {
    (void)ctx; json_t *questions=json_object_get(request,"questions"),*state=json_object_get(request,"state");
    json_t *answers=json_object(); CHECK(answers); size_t count=json_object_size(questions);
    const char *keys[48]; json_t *v; const char *key; size_t i=0;
    json_object_foreach(questions,key,v) { CHECK(i<48); keys[i++]=key; }
    /* Response object ordering must not affect ID -> probability -> source mapping. */
    for (size_t j=count;j>0;j--) {
        key=keys[j-1]; size_t line=0,token=0;
        int parsed=sscanf(key,"line_%zu_token_%zu",&line,&token); CHECK(parsed>=1&&line<3);
        json_t *q=json_object_get(questions,key),*criteria=json_object_get(q,"criteria");
        CHECK(json_is_string(json_object_get(criteria,"true"))&&json_is_string(json_object_get(criteria,"false")));
        double probability=line_prob[line];
        if (parsed==2) {
            CHECK(line>0&&token<5); probability=word_prob[line][token];
            CHECK(!json_object_get(state,"original_lyrics")); CHECK(json_object_size(state)==5);
            CHECK(json_is_string(json_object_get(state,"original_line")));
            CHECK(strstr(json_string_value(json_object_get(json_object_get(q,"instructions"),"task")),"YES means redact"));
            CHECK(strstr(json_string_value(json_object_get(criteria,"true")),"requiring minimal redaction"));
            CHECK(strstr(json_string_value(json_object_get(criteria,"false")),"preserved"));
            json_t *tokens=json_object_get(state,"ordered_word_tokens");
            CHECK((size_t)json_integer_value(json_object_get(json_array_get(tokens,token),"token_id"))==token);
            if (line==1) {
                CHECK(!strcmp(json_string_value(json_object_get(state,"previous_line")),"skip abc"));
                CHECK(!strcmp(json_string_value(json_object_get(state,"next_line")),"same same same"));
                CHECK(json_array_size(tokens)==5);
            }
        }
        CHECK(json_object_set_new(answers,key,json_pack("{s:s,s:f}","type","noul","noul",probability))==0);
    }
    json_t *response=json_pack("{s:o}","answers",answers); CHECK(response);
    bool ok=response_validate(response,questions,p,error); json_decref(response); return ok;
}
static json_t *load_report(int dirfd,const char *name) {
    int fd=openat(dirfd,name,O_RDONLY); CHECK(fd>=0); json_error_t error;
    json_t *r=json_loadfd(fd,JSON_REJECT_DUPLICATES,&error); close(fd); CHECK(r); return r;
}
static bool zero_decide(void *ctx,const json_t *request,double *p,char *error) {
    (void)ctx; json_t *questions=json_object_get(request,"questions"),*answers=json_object(); CHECK(answers);
    const char *key; json_t *q;
    json_object_foreach(questions,key,q) {
        (void)q; size_t line,token; int parts=sscanf(key,"line_%zu_token_%zu",&line,&token); CHECK(parts>=1);
        double probability=parts==2?0.74:(line<31?0.3:0.1);
        CHECK(json_object_set_new(answers,key,json_pack("{s:s,s:f}","type","noul","noul",probability))==0);
    }
    json_t *response=json_pack("{s:o}","answers",answers); CHECK(response);
    bool ok=response_validate(response,questions,p,error); json_decref(response); return ok;
}
int main(void) {
    Config cfg; config_init(&cfg); strcpy(cfg.model,"typesafe/jev-1.13");
    Diagnostics *d=diagnostics_create(input,sizeof(input)-1,&cfg); CHECK(d);
    atomic_bool cancel; atomic_init(&cancel,false); Progress progress={0}; Buffer out={0}; char error[LY_ERROR];
    CHECK(classifier_run(input,sizeof(input)-1,&cfg,d,&cancel,&progress,NULL,NULL,decide,NULL,&out,error));
    const char expected[]="skip abc\r\nfoo  *-*** ***'* abc123 *\n**** same ****";
    CHECK(out.len==sizeof(expected)-1&&!memcmp(out.data,expected,out.len));
    CHECK(progress.lines_done==3&&progress.candidates==2&&progress.tokens_done==8&&progress.tokens_marked==5);
    CHECK(cfg.line_threshold==0.20&&cfg.token_threshold==0.75); /* No tuning as a side effect. */
    DiagnosticRecord *r=NULL; size_t count; bool complete;
    CHECK(diagnostics_snapshot(d,1,&r,&count,&complete)&&complete&&count==3);
    for (size_t i=0;i<count;i++) { CHECK(r[i].probability==line_prob[i]); CHECK(r[i].selected==(i>0)); } free(r);
    CHECK(diagnostics_snapshot(d,2,&r,&count,&complete)&&complete&&count==8);
    CHECK(r[0].probability==1&&r[0].line_id==1&&r[0].token_id==4);
    CHECK(r[1].probability==0.99&&r[1].line_id==2&&r[1].token_id==0);
    for (size_t i=0;i<count;i++) {
        CHECK(r[i].probability==word_prob[r[i].line_id][r[i].token_id]);
        CHECK(r[i].selected==(r[i].probability>=cfg.token_threshold));
        if (i) CHECK(r[i-1].probability>=r[i].probability);
        CHECK(r[i].end<=sizeof(input)-1&&r[i].start<r[i].end);
    }
    free(r); buf_free(&out);
    char dir[]="/tmp/lyrcensor-diagnostics-XXXXXX"; CHECK(mkdtemp(dir)); int fd=open(dir,O_RDONLY|O_DIRECTORY); CHECK(fd>=0); char *name=NULL;
    setenv("OPENROUTER_API_KEY","diagnostic-test-secret-never-copy",1);
    CHECK(diagnostics_save(d,fd,&name,error)); CHECK(!strcmp(name,"LYRCENSOR diagnostics.json")); json_t *report=load_report(fd,name); free(name);
    CHECK(json_is_true(json_object_get(report,"classification_complete")));
    CHECK(json_number_value(json_object_get(report,"stage1_threshold"))==cfg.line_threshold);
    CHECK(json_number_value(json_object_get(report,"stage2_threshold"))==cfg.token_threshold);
    CHECK(json_array_size(json_object_get(report,"lines"))==3);
    json_t *tokens=json_object_get(report,"tokens"); CHECK(json_array_size(tokens)==8);
    for (size_t i=0;i<8;i++) {
        json_t *t=json_array_get(tokens,i); size_t start=(size_t)json_integer_value(json_object_get(t,"byte_start")),end=(size_t)json_integer_value(json_object_get(t,"byte_end"));
        const char *original=json_string_value(json_object_get(t,"original"));
        CHECK(json_string_length(json_object_get(t,"original"))==end-start&&!memcmp(input+start,original,end-start));
        CHECK(json_is_true(json_object_get(t,"span_verified")));
    }
    char *serialized=json_dumps(report,0); CHECK(serialized&&!strstr(serialized,"diagnostic-test-secret-never-copy")); free(serialized); json_decref(report);
    CHECK(diagnostics_save(d,fd,&name,error)); CHECK(!strcmp(name,"LYRCENSOR diagnostics (1).json")); free(name);
    FILE *printed=tmpfile(); CHECK(printed&&diagnostics_print(d,printed,error)); fclose(printed);
    unlinkat(fd,"LYRCENSOR diagnostics.json",0); unlinkat(fd,"LYRCENSOR diagnostics (1).json",0); close(fd); CHECK(rmdir(dir)==0);
    diagnostics_free(d);
    /* Reproduce 58 lines -> 31 candidates -> 149 evaluated words -> zero masks. */
    Buffer synthetic={0}; for (size_t i=0;i<58;i++) {
        size_t words=i<25?5:4;
        for (size_t j=0;j<words;j++) CHECK(buf_append(&synthetic,"word ",5));
        CHECK(buf_append(&synthetic,"\n",1));
    }
    d=diagnostics_create(synthetic.data,synthetic.len,&cfg); CHECK(d); progress=(Progress){0};
    CHECK(classifier_run(synthetic.data,synthetic.len,&cfg,d,&cancel,&progress,NULL,NULL,zero_decide,NULL,&out,error));
    CHECK(progress.lines_done==58&&progress.candidates==31&&progress.tokens_done==149&&progress.tokens_marked==0);
    CHECK(out.len==synthetic.len&&!memcmp(out.data,synthetic.data,out.len)); buf_free(&out);
    CHECK(diagnostics_snapshot(d,2,&r,&count,&complete)&&complete&&count==149&&r[0].probability==0.74);
    for (size_t i=0;i<count;i++) CHECK(!r[i].selected);
    free(r); diagnostics_free(d);
    /* Threshold extremes and very near-boundary probabilities must stay literal. */
    d=diagnostics_create("x",1,&cfg); CHECK(d);
    CHECK(diagnostics_record(d,2,0,0,0,1,nextafter(cfg.token_threshold,0)));
    CHECK(diagnostics_record(d,2,0,1,0,1,cfg.token_threshold));
    CHECK(diagnostics_record(d,2,0,2,0,1,nextafter(cfg.token_threshold,1)));
    CHECK(!diagnostics_record(d,2,0,3,0,2,1)); CHECK(!diagnostics_record(d,2,0,3,0,1,NAN));
    CHECK(diagnostics_snapshot(d,2,&r,&count,&complete)&&!complete&&count==3);
    CHECK(r[0].selected&&r[1].selected&&!r[2].selected); free(r); diagnostics_free(d); buf_free(&synthetic);
    puts("PASS: exact probabilities, YES semantics, unchanged inclusive thresholds, reversed answer IDs, token spans/replacement, sorted diagnostics, reports."); return 0;
}
