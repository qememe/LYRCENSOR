#include "classifier.h"
#include "output.h"
#include "unicode.h"
#include <assert.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while (0)
static void equal(const Buffer *b,const char *s) { CHECK(b->len==strlen(s)); CHECK(!memcmp(b->data,s,b->len)); }
static Buffer redact(const char *s,bool all) {
    Lines lines={0}; Tokens tokens={0},all_tokens={0}; CHECK(split_lines(s,strlen(s),&lines));
    for (size_t i=0;i<lines.len;i++) {
        CHECK(tokenize_line(s,lines.lines[i],i,&tokens));
        if (tokens.len) {
            Token *p=realloc(all_tokens.items,(all_tokens.len+tokens.len)*sizeof(Token)); CHECK(p); all_tokens.items=p;
            for (size_t j=0;j<tokens.len;j++) { Token t=tokens.items[j]; CHECK(t.id==j&&t.line_id==i); t.selected=all; all_tokens.items[all_tokens.len++]=t; }
        }
        tokens_free(&tokens);
    }
    Buffer b={0}; CHECK(censor_text(s,strlen(s),all_tokens.items,all_tokens.len,&b));
    tokens_free(&all_tokens); free(lines.lines); return b;
}
static void test_unicode_censor(void) {
    struct { const char *s,*masked; } cases[]={
        {"hello, world!", "*****, *****!"},
        {"привет", "******"},{"Привет hello ёЁ", "****** ***** **"},
        {"какое-то hello-world ё-моё", "*****-** *****-***** *-***"},
        {"can't l’amour rock'n'roll", "***'* *’***** ****'*'****"},
        {"abc123 123абв 123", "***123 123*** 123"},
        {"e\xcc\x81 и\xcc\x86 ё", "* * *"},
        {"a\xcc\x81\xcc\x82-1\xcc\x81 😀 a❤️", "*-1\xcc\x81 😀 *❤️"},
        {"\n\n\t  x\n\n", "\n\n\t  *\n\n"},
        {"A\r\n\r\nБ\t  C", "*\r\n\r\n*\t  *"},
        {"one\rtwo\nlast", "***\r***\n****"},
        {"1234 -- 😀 \t", "1234 -- 😀 \t"},
        {"", ""}
    };
    for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        Buffer a=redact(cases[i].s,false); equal(&a,cases[i].s); buf_free(&a);
        a=redact(cases[i].s,true); equal(&a,cases[i].masked); buf_free(&a);
    }
    CHECK(u_valid_text("ё\t\n\r",5)); CHECK(!u_valid_text("\xc0\xaf",2)); CHECK(!u_valid_text("a\0b",3));
    CHECK(!u_valid_text("\033[15~",5)); CHECK(!u_valid_text("\xc2\x85",2));
    CHECK(u_next("е\xcc\x81x",5,0)==4); CHECK(u_prev("е\xcc\x81x",5,4)==0);
    CHECK(u_next("\r\nx",3,0)==2); CHECK(u_width("界",0,3,0)==2);
    CHECK(u_width("e\xcc\x81",0,3,0)==1); CHECK(u_width("\t",0,1,3)==5);
    const char *family="👩‍👩‍👧‍👦"; CHECK(u_next(family,strlen(family),0)==strlen(family)); CHECK(u_width(family,0,strlen(family),0)==2);
    Token bad={0,0,3,1,true}; Buffer out={0}; CHECK(!censor_text("abc",3,&bad,1,&out));
}
static void test_tokenizer(void) {
    const char *s="--a--b 'can't' 123 abc123 ё-моё e\xcc\x81 l’amour 1-2-abc 😀";
    Lines ls={0}; Tokens ts={0}; CHECK(split_lines(s,strlen(s),&ls)); CHECK(ls.len==1);
    CHECK(tokenize_line(s,ls.lines[0],0,&ts));
    const char *words[]={"a","b","can't","abc123","ё-моё","e\xcc\x81","l’amour","1-2-abc"};
    CHECK(ts.len==sizeof(words)/sizeof(words[0]));
    for (size_t i=0;i<ts.len;i++) { Token t=ts.items[i]; CHECK(t.id==i); CHECK(t.end-t.start==strlen(words[i])); CHECK(!memcmp(s+t.start,words[i],t.end-t.start)); }
    tokens_free(&ts); free(ls.lines);
    CHECK(split_lines("a\r\n\r\n\n",6,&ls)); CHECK(ls.len==4); CHECK(ls.lines[0].end==1&&ls.lines[0].next==3); CHECK(ls.lines[1].start==3&&ls.lines[1].next==5); free(ls.lines);
}
static void test_editor(void) {
    Editor e={0}; CHECK(editor_insert(&e,"one\r\n\r\nе\xcc\x81\t界\nlast",strlen("one\r\n\r\nе\xcc\x81\t界\nlast")));
    editor_key(&e,ED_HOME,5); CHECK(e.cursor==strlen("one\r\n\r\nе\xcc\x81\t界\n"));
    editor_key(&e,ED_UP,5); CHECK(editor_column(&e)==0);
    editor_key(&e,ED_RIGHT,5); CHECK(editor_column(&e)==1); CHECK(e.cursor==11);
    editor_key(&e,ED_RIGHT,5); CHECK(editor_column(&e)==8);
    editor_key(&e,ED_RIGHT,5); CHECK(editor_column(&e)==10);
    editor_key(&e,ED_BACK,5); CHECK(editor_column(&e)==8);
    editor_key(&e,ED_HOME,5); editor_key(&e,ED_DELETE,5);
    CHECK(!strstr(e.text.data,"е\xcc\x81")); CHECK(strstr(e.text.data,"\r\n\r\n\t\n"));
    editor_key(&e,ED_UP,5); editor_key(&e,ED_BACK,5);
    equal(&e.text,"one\r\n\t\nlast");
    CHECK(!editor_insert(&e,"\033[15~",5)); equal(&e.text,"one\r\n\t\nlast");
    buf_free(&e.text); e=(Editor){0};
    CHECK(editor_insert(&e,"abc\n12\nabcdef",13));
    editor_key(&e,ED_UP,2); CHECK(editor_column(&e)==2); editor_key(&e,ED_UP,2); CHECK(editor_column(&e)==3);
    editor_key(&e,ED_HOME,2); editor_key(&e,ED_PGDN,2); CHECK(e.cursor==7);
    buf_free(&e.text); e=(Editor){0};
    CHECK(editor_insert(&e,"👩‍👩‍👧‍👦e\xcc\x81",strlen("👩‍👩‍👧‍👦e\xcc\x81")));
    editor_key(&e,ED_LEFT,2); CHECK(e.cursor==strlen("👩‍👩‍👧‍👦")); editor_key(&e,ED_BACK,2); equal(&e.text,"e\xcc\x81");
    buf_free(&e.text); e=(Editor){0};
    CHECK(editor_insert(&e,"e\n\xcc\x81",4)); e.cursor=1; editor_key(&e,ED_DELETE,2);
    equal(&e.text,"e\xcc\x81"); CHECK(e.cursor==3); editor_key(&e,ED_BACK,2); equal(&e.text,"");
    CHECK(editor_insert(&e,"\n",1)); e.cursor=0; CHECK(editor_insert(&e,"\r",1)); CHECK(e.cursor==2);
    editor_key(&e,ED_BACK,2); equal(&e.text,""); buf_free(&e.text);
}
static json_t *parse(const char *s) { char error[LY_ERROR]; json_t *j=response_parse(s,strlen(s),error); CHECK(j); return j; }
static void test_responses(void) {
    json_t *questions=parse("{\"line_0_token_0\":{},\"line_0_token_1\":{}}");
    const char *bad[]={
        "{}", "{\"answers\":[]}",
        "{\"answers\":{\"line_0_token_0\":{\"type\":\"noul\",\"noul\":0.5}}}",
        "{\"answers\":{\"line_0_token_0\":{\"type\":\"noul\",\"noul\":0},\"unknown\":{\"type\":\"noul\",\"noul\":1}}}",
        "{\"answers\":{\"line_0_token_0\":{\"type\":\"noul\",\"noul\":0},\"line_0_token_1\":{\"type\":\"noul\",\"noul\":1.1}}}",
        "{\"answers\":{\"line_0_token_0\":{\"type\":\"noul\",\"noul\":0},\"line_0_token_1\":{\"type\":\"noul\",\"noul\":true}}}",
        "{\"answers\":{\"line_0_token_0\":{\"type\":\"noul\",\"noul\":0},\"line_0_token_1\":{\"type\":\"choice\",\"noul\":1}}}",
        "{\"error\":{\"message\":\"refused\"}}"
    };
    char error[LY_ERROR]; double p[2];
    for (size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) { json_t *r=parse(bad[i]); p[0]=p[1]=-99; CHECK(!response_validate(r,questions,p,error)); CHECK(p[0]==-99&&p[1]==-99); json_decref(r); }
    CHECK(!response_parse("not json",8,error));
    const char *dup="{\"answers\":{\"x\":{},\"x\":{}}}"; CHECK(!response_parse(dup,strlen(dup),error));
    json_t *r=parse("{\"answers\":{\"line_0_token_1\":{\"type\":\"noul\",\"noul\":1},\"line_0_token_0\":{\"type\":\"noul\",\"noul\":0.2}},\"usage\":{\"input_tokens\":123,\"output_tokens\":2,\"cost\":0.001}}");
    CHECK(response_validate(r,questions,p,error)); CHECK(p[0]==0.2&&p[1]==1);
    Usage u={0}; usage_add(&u,r); usage_add(&u,r); CHECK(u.input_tokens==246&&u.output_tokens==4&&u.cost==0.002&&u.cost_reports==2);
    json_decref(r); json_decref(questions);
    CHECK(api_retry_status(429)&&api_retry_status(503)&&!api_retry_status(401)&&!api_retry_status(400)&&!api_retry_status(403));
}
typedef struct { size_t calls,line_questions,word_questions; int mode; bool cancel_after; atomic_bool *cancel; } Mock;
static bool mock_decide(void *ctx,const json_t *req,double *probs,char *error) {
    Mock *m=ctx; m->calls++;
    CHECK(!strcmp(json_string_value(json_object_get(req,"model")),"typesafe/jev-1.13"));
    CHECK(json_object_size(req)==3); json_t *qs=json_object_get(req,"questions"),*state=json_object_get(req,"state");
    CHECK(json_is_object(state)&&json_is_object(qs));
    json_t *answers=json_object(); CHECK(answers); const char *key; json_t *v;
    json_object_foreach(qs,key,v) {
        CHECK(!strcmp(json_string_value(json_object_get(v,"type")),"noul"));
        CHECK(json_object_get(json_object_get(v,"instructions"),"policy"));
        bool word=strstr(key,"_token_")!=NULL;
        if (word) {
            m->word_questions++; json_t *context=json_object_get(state,"context");
            CHECK(json_is_array(json_object_get(context,"ordered_word_tokens")));
            CHECK(!json_object_get(state,"original_lyrics")); CHECK(json_object_get(context,"previous_line")); CHECK(json_object_get(context,"next_line"));
            CHECK(json_is_object(json_object_get(json_object_get(state,"target_tokens"),key)));
        }
        else { m->line_questions++; CHECK(json_is_array(json_object_get(state,"lyric_lines"))); }
        double value=m->mode==0?0:m->mode==1?1:(word?0.75:0.20);
        json_t *a=json_pack("{s:s,s:f}","type","noul","noul",value); CHECK(a); CHECK(json_object_set_new(answers,key,a)==0);
    }
    json_t *r=json_pack("{s:o}","answers",answers); CHECK(r);
    bool ok=response_validate(r,qs,probs,error); json_decref(r);
    if (m->cancel_after) atomic_store(m->cancel,true);
    return ok;
}
static void test_pipeline(void) {
    const char *s="First\r\n\r\nПривет  hello-world\tё-моё!\nlast"; Config cfg; config_init(&cfg); strcpy(cfg.model,"typesafe/jev-1.13");
    atomic_bool cancel; atomic_init(&cancel,false); char error[LY_ERROR];
    for (int mode=0;mode<3;mode++) {
        Progress p={0}; Buffer out={0}; Mock m={.mode=mode,.cancel=&cancel};
        CHECK(classifier_run(s,strlen(s),&cfg,NULL,&cancel,&p,NULL,NULL,mock_decide,&m,&out,error));
        CHECK(m.line_questions==3&&p.lines_done==3&&p.lines_total==3);
        if (!mode) { equal(&out,s); CHECK(!m.word_questions&&!p.candidates); }
        else { equal(&out,"*****\r\n\r\n******  *****-*****\t*-***!\n****"); CHECK(p.tokens_marked==5&&m.word_questions==5&&p.candidates_done==3); }
        buf_free(&out);
    }
    /* Multi-request line and word batches, with complete context on every word batch. */
    Buffer big={0}; for (size_t i=0;i<80;i++) CHECK(buf_append(&big,"abc ",4)); CHECK(buf_append(&big,"\n",1));
    for (size_t i=0;i<60;i++) CHECK(buf_append(&big,"x\n",2));
    Progress p={0}; Buffer out={0}; Mock m={.mode=1,.cancel=&cancel};
    CHECK(classifier_run(big.data,big.len,&cfg,NULL,&cancel,&p,NULL,NULL,mock_decide,&m,&out,error));
    CHECK(m.line_questions==61&&m.word_questions==140&&m.calls>4); buf_free(&big); buf_free(&out);
    m=(Mock){.mode=1,.cancel_after=true,.cancel=&cancel}; p=(Progress){0};
    CHECK(!classifier_run(s,strlen(s),&cfg,NULL,&cancel,&p,NULL,NULL,mock_decide,&m,&out,error)); CHECK(!out.data); atomic_store(&cancel,false);
    CHECK(!classifier_run("",0,&cfg,NULL,&cancel,&p,NULL,NULL,mock_decide,&m,&out,error));
    /* Extremely long line, no final newline, Unicode near far end, no truncation. */
    size_t len=1024*1024; char *longline=malloc(len+3); CHECK(longline); memset(longline,'a',len); memcpy(longline+len,"ё",3);
    cfg.line_threshold=1; m=(Mock){.mode=0,.cancel=&cancel}; p=(Progress){0};
    CHECK(classifier_run(longline,len+2,&cfg,NULL,&cancel,&p,NULL,NULL,mock_decide,&m,&out,error)); CHECK(out.len==len+2&&!memcmp(out.data,longline,len+2));
    buf_free(&out); Buffer red=redact(longline,true); CHECK(red.len==len+1); for (size_t i=0;i<red.len;i++) CHECK(red.data[i]=='*');
    buf_free(&red); free(longline);
}
static void test_output(void) {
    struct {const char *source,*base;} cases[]={
        {"  Моя песня \t\r\nbody","Моя песня"},{"../a\\b:*?\"<>|\nX","_._a_b_______"},
        {"\nbody","lyrics"},{"  \t\n","lyrics"},{"..\n","lyrics"},{".hidden\n","_hidden"},{"name...  \n","name"}
    };
    for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) { char *b=output_basename(cases[i].source,strlen(cases[i].source),200); CHECK(b); if (strcmp(b,cases[i].base)) fprintf(stderr,"filename got [%s] expected [%s]\n",b,cases[i].base); CHECK(!strcmp(b,cases[i].base)); free(b); }
    char *b=output_basename("ёёёёёёёёё",18,7); CHECK(b&&strlen(b)==6&&u_valid_text(b,strlen(b))); free(b);
    char dir[]="/tmp/lyrcensor-core-XXXXXX"; CHECK(mkdtemp(dir)); int fd=open(dir,O_RDONLY|O_DIRECTORY); CHECK(fd>=0);
    Buffer content={0}; CHECK(buf_append(&content,"Текст\r\n\n\t  end",strlen("Текст\r\n\n\t  end"))); char error[LY_ERROR],*name=NULL;
    CHECK(output_save(fd,"Song\n",5,&content,&name,error)); CHECK(!strcmp(name,"Song.md")); free(name);
    CHECK(output_save(fd,"Song\n",5,&content,&name,error)); CHECK(!strcmp(name,"Song (1).md"));
    int input=openat(fd,name,O_RDONLY); CHECK(input>=0); char data[128]; ssize_t n=read(input,data,sizeof(data)); CHECK(n==(ssize_t)content.len&&!memcmp(data,content.data,content.len)); close(input); free(name);
    CHECK(symlinkat("/dev/null",fd,"Link.md")==0); CHECK(output_save(fd,"Link",4,&content,&name,error)); CHECK(!strcmp(name,"Link (1).md")); free(name);
    CHECK(!output_save(-1,"test",4,&content,&name,error));
    unlinkat(fd,"Song.md",0); unlinkat(fd,"Song (1).md",0); unlinkat(fd,"Link.md",0); unlinkat(fd,"Link (1).md",0); close(fd); CHECK(rmdir(dir)==0); buf_free(&content);
}
int main(void) {
    setlocale(LC_CTYPE,"C.UTF-8"); test_unicode_censor(); test_tokenizer(); test_editor(); test_responses(); test_pipeline(); test_output();
    puts("PASS: Unicode, tokenization, exact redaction, editor, Decisions validation, two-stage pipeline, output safety."); return 0;
}
