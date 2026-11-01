#include "classifier.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while (0)
/* Synthetic request fixtures, not semantic assertions about mocked probabilities.
 * Human-reference masking labels and live evaluation steps are in docs/policy-evaluation.md. */
typedef struct { const char *name, *previous, *current, *next, *target; } Case;
static const Case cases[]={
    {"identity slur", "Спор перешёл в оскорбления.", "Он обозвал соседей пидарасами.", "Они ушли.", "пидарасами"},
    {"neutral identity", "Мы познакомились вчера.", "Он гей и пишет песни.", "Завтра концерт.", "гей"},
    {"informal identity", "Мы читаем статью.", "В статье упомянули лесбиян и геев.", "Речь о фестивале.", "лесбиян"},
    {"identity metaphor", "Ведущий издевался над их ориентацией.", "Он унизительно назвал геев петушками.", "Насмешка касалась всей группы.", "петушками"},
    {"literal metaphor counterpart", "Мы пришли на ферму.", "Петушки клюют зерно во дворе.", "Рядом ходят куры.", "Петушки"},
    {"ordinary profanity", "Снова начался дождь.", "Мне похуй на дождь.", "Я всё равно иду гулять.", "похуй"},
    {"general vulgar insult", "Мы снова спорим о ключах.", "Да ты ебанат, опять забыл ключи.", "Я оставил запасные дома.", "ебанат"},
    {"quoted slur", "Рассказчик цитирует оскорбление.", "Он крикнул: «Вы пидарасы!»", "Он осуждает эту реплику.", "пидарасы"},
    {"satirical slur", "Сатирик высмеивает нетерпимого героя.", "Наш герой орёт: «Долой пидарасов!»", "Публика понимает иронию.", "пидарасов"},
    {"neutral term in hostile satire", "Сатирик высмеивает нетерпимого героя.", "Наш герой орёт: «Долой геев!»", "Публика понимает иронию.", "геев"},
    {"non-graphic historical violence", "Мы читаем историю города.", "После войны город долго восстанавливали.", "Люди помогали друг другу.", "войны"},
    {"religious criticism", "Мы обсуждаем религию.", "Я считаю церковную доктрину ошибочной.", "Это критика идей.", "церковную"},
    {"neutral word in hostile sentence", "Музыка мешает мне спать.", "Ненавижу этих соседей и их музыку.", "Попрошу их сделать тише.", "соседей"},
    {"Russian spelling variant", "Спор перешёл в оскорбления.", "Он обзывал геев пидорасами.", "Они ушли.", "пидорасами"},
    {"ethnic slur", "Спор перешёл в оскорбления.", "Он оскорбил узбеков словом «чурки».", "Они ушли.", "чурки"},
    {"literal ethnic-slur counterpart", "Мы принесли дрова.", "У печи лежали берёзовые чурки.", "Надо растопить печь.", "чурки"}
};
typedef struct {
    const char *source;
    Lines lines;
    const Case *fixture;
    bool line_seen[5], token_seen[5][128];
    size_t focus_targets, focus_batches;
} Capture;
static bool same_span(json_t *v,const char *s,size_t start,size_t end) {
    return json_is_string(v)&&json_string_length(v)==end-start&&!memcmp(json_string_value(v),s+start,end-start);
}
static bool expanded_matches(const char *actual,const char *format,const char *placeholder,const char *value) {
    const char *where=strstr(format,placeholder); CHECK(where);
    size_t prefix=(size_t)(where-format),value_len=strlen(value),suffix=strlen(where+strlen(placeholder));
    return strlen(actual)==prefix+value_len+suffix&&!memcmp(actual,format,prefix)&&
        !memcmp(actual+prefix,value,value_len)&&!strcmp(actual+prefix+value_len,where+strlen(placeholder));
}
static void check_context(Capture *c,json_t *context,size_t line) {
    CHECK(line<c->lines.len); Line l=c->lines.lines[line];
    CHECK((size_t)json_integer_value(json_object_get(context,"line_id"))==line);
    CHECK(same_span(json_object_get(context,"original_line"),c->source,l.start,l.end));
    json_t *prev=json_object_get(context,"previous_line"),*next=json_object_get(context,"next_line");
    if (!line) CHECK(json_is_null(prev));
    else { Line p=c->lines.lines[line-1]; CHECK(same_span(prev,c->source,p.start,p.end)); }
    if (line+1==c->lines.len) CHECK(json_is_null(next));
    else { Line n=c->lines.lines[line+1]; CHECK(same_span(next,c->source,n.start,n.end)); }
}
static bool inspect_request(void *ctx,const json_t *request,double *probabilities,char *error) {
    Capture *c=ctx; json_t *qs=json_object_get(request,"questions"),*state=json_object_get(request,"state");
    CHECK(json_object_size(request)==3); CHECK(!strcmp(json_string_value(json_object_get(request,"model")),"typesafe/jev-1.13"));
    json_t *context=json_object_get(state,"context"),*targets=json_object_get(state,"target_tokens"); bool word=context!=NULL;
    if (word) {
        CHECK(json_object_size(state)==2&&json_object_size(context)==5);
        CHECK(json_object_size(targets)==json_object_size(qs));
        size_t line=(size_t)json_integer_value(json_object_get(context,"line_id")); check_context(c,context,line);
        if (line==2) c->focus_batches++;
    } else {
        CHECK(json_object_size(state)==1);
        json_t *lines=json_object_get(state,"lyric_lines"); CHECK(json_array_size(lines)==5);
        for (size_t i=0;i<5;i++) check_context(c,json_array_get(lines,i),i);
    }
    json_t *answers=json_object(); CHECK(answers); const char *key; json_t *q;
    json_object_foreach(qs,key,q) {
        CHECK(json_object_size(q)==3&&!strcmp(json_string_value(json_object_get(q,"type")),"noul"));
        json_t *instructions=json_object_get(q,"instructions"),*criteria=json_object_get(q,"criteria");
        CHECK(json_object_size(instructions)==2&&json_object_size(criteria)==2);
        CHECK(!strcmp(json_string_value(json_object_get(instructions,"policy")),classifier_policy));
        CHECK(!strcmp(json_string_value(json_object_get(criteria,"true")),word?classifier_stage2_yes:classifier_stage1_yes));
        CHECK(!strcmp(json_string_value(json_object_get(criteria,"false")),word?classifier_stage2_no:classifier_stage1_no));
        size_t line=5,token=0; int parts=sscanf(key,"line_%zu_token_%zu",&line,&token); CHECK(parts>=1&&line<5);
        const char *task=json_string_value(json_object_get(instructions,"task")); CHECK(task);
        if (word) {
            CHECK(parts==2&&token<128&&!c->token_seen[line][token]); c->token_seen[line][token]=true;
            CHECK(expanded_matches(task,classifier_stage2_task,"%s",key));
            CHECK((size_t)json_integer_value(json_object_get(context,"line_id"))==line);
            Tokens expected={0}; CHECK(tokenize_line(c->source,c->lines.lines[line],line,&expected)); CHECK(token<expected.len);
            json_t *ordered=json_object_get(context,"ordered_word_tokens"); CHECK(json_array_size(ordered)==expected.len);
            for (size_t i=0;i<expected.len;i++) {
                json_t *t=json_array_get(ordered,i); Token e=expected.items[i];
                CHECK((size_t)json_integer_value(json_object_get(t,"token_id"))==i);
                CHECK(same_span(json_object_get(t,"original"),c->source,e.start,e.end));
            }
            json_t *target=json_object_get(targets,key); CHECK(json_object_size(target)==3);
            CHECK((size_t)json_integer_value(json_object_get(target,"line_id"))==line);
            CHECK((size_t)json_integer_value(json_object_get(target,"token_id"))==token);
            Token e=expected.items[token]; CHECK(same_span(json_object_get(target,"original"),c->source,e.start,e.end));
            CHECK(json_equal(json_object_get(target,"original"),json_object_get(json_array_get(ordered,token),"original")));
            if (line==2&&!strcmp(json_string_value(json_object_get(target,"original")),c->fixture->target)) c->focus_targets++;
            tokens_free(&expected);
        } else {
            CHECK(parts==1&&!c->line_seen[line]); c->line_seen[line]=true; char id[32]; snprintf(id,sizeof(id),"%zu",line);
            CHECK(expanded_matches(task,classifier_stage1_task,"%zu",id));
        }
        /* Deliberately unrelated to the fixture's meaning. Verify construction and byte preservation only. */
        CHECK(json_object_set_new(answers,key,json_pack("{s:s,s:f}","type","noul","noul",word?0.0:1.0))==0);
    }
    json_t *response=json_pack("{s:o}","answers",answers); CHECK(response);
    bool ok=response_validate(response,qs,probabilities,error); json_decref(response); return ok;
}
static void run_case(const Case *fixture,size_t expected_targets,size_t expected_batches) {
    Buffer source={0}; const char *parts[]={"Внешний пролог.",fixture->previous,fixture->current,fixture->next,"Внешний эпилог."};
    for (size_t i=0;i<5;i++) { if (i) CHECK(buf_append(&source,"\r\n",2)); CHECK(buf_append(&source,parts[i],strlen(parts[i]))); }
    Capture capture={.source=source.data,.fixture=fixture}; CHECK(split_lines(source.data,source.len,&capture.lines)&&capture.lines.len==5);
    Config cfg; config_init(&cfg); strcpy(cfg.model,"typesafe/jev-1.13"); CHECK(cfg.line_threshold==0.20&&cfg.token_threshold==0.75);
    Progress progress={0}; atomic_bool cancel; atomic_init(&cancel,false); Buffer out={0}; char error[LY_ERROR];
    CHECK(classifier_run(source.data,source.len,&cfg,NULL,&cancel,&progress,NULL,NULL,inspect_request,&capture,&out,error));
    CHECK(out.len==source.len&&!memcmp(out.data,source.data,source.len));
    CHECK(capture.focus_targets==expected_targets&&capture.focus_batches==expected_batches);
    for (size_t i=0;i<5;i++) {
        CHECK(capture.line_seen[i]); Tokens tokens={0}; CHECK(tokenize_line(source.data,capture.lines.lines[i],i,&tokens));
        for (size_t j=0;j<tokens.len;j++) CHECK(capture.token_seen[i][j]);
        tokens_free(&tokens);
    }
    CHECK(cfg.line_threshold==0.20&&cfg.token_threshold==0.75);
    free(capture.lines.lines); buf_free(&source); buf_free(&out);
}
int main(void) {
    for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) run_case(&cases[i],1,1);
    Buffer longline={0}; for (size_t i=0;i<53;i++) CHECK(buf_append(&longline,"повтор ",strlen("повтор ")));
    Case batched={"repeated words across batches","До строки.",longline.data,"После строки.","повтор"};
    run_case(&batched,53,2); buf_free(&longline);
    puts("PASS: Russian request/context/target construction, exact policy templates, null neighbors, repeated-token batch IDs (not semantic accuracy)."); return 0;
}
