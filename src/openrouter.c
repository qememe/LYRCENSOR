#include "openrouter.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <errno.h>
#include <stdint.h>
void progress_notify(Progress *p, ProgressFn fn, void *ctx, const char *activity) {
    if (activity) snprintf(p->activity,sizeof(p->activity),"%s",activity);
    p->revision++;
    if (fn) fn(ctx,p);
}
static bool cancelled(ApiClient *a) { return atomic_load(a->cancel)||ly_signal; }
static void activity(ApiClient *a, const char *s) { progress_notify(a->progress,a->notify,a->notify_ctx,s); }
bool api_retry_status(long s) { return s==408||s==425||s==429||s==500||s==502||s==503||s==504||s==529; }
bool api_init(ApiClient *a, char *error) {
    if (!a->config || !a->cancel || !a->progress || !config_key_valid(a->key)) {
        error_set(error,"Missing API client configuration or invalid OPENROUTER_API_KEY."); return false;
    }
    if (!config_valid(a->config,error)) return false;
#ifdef LY_TEST_HTTP
    if (!a->test_url) a->test_url=getenv("LYRCENSOR_TEST_URL");
#endif
    a->handle=curl_easy_init();
    if (!a->handle) { error_set(error,"Cannot initialize HTTPS client (out of memory)."); return false; }
    return true;
}
void api_cleanup(ApiClient *a) { if (a->handle) curl_easy_cleanup(a->handle); a->handle=NULL; }
typedef struct { Buffer body; ApiClient *api; unsigned retry_after; bool overflow, announced; } Transfer;
static size_t receive(char *p,size_t size,size_t nmemb,void *ctx) {
    Transfer *t=ctx;
    if (size && nmemb>SIZE_MAX/size) return 0;
    size_t n=size*nmemb;
    if (cancelled(t->api)) return 0;
    if (!buf_append(&t->body,p,n)) { t->overflow=true; return 0; }
    if (!t->announced && n) { activity(t->api,"Receiving Decisions response..."); t->announced=true; }
    return n;
}
static size_t header(char *p,size_t size,size_t nmemb,void *ctx) {
    Transfer *t=ctx;
    if (size && nmemb>SIZE_MAX/size) return 0;
    size_t n=size*nmemb;
    if (n>12 && !strncasecmp(p,"Retry-After:",12)) {
        /* Numeric delay only; bounded to avoid a server holding the UI indefinitely. */
        char b[32]; size_t k=n-12; if (k>=sizeof(b)) k=sizeof(b)-1;
        memcpy(b,p+12,k); b[k]=0; char *end; errno=0; unsigned long v=strtoul(b,&end,10);
        if (!errno&&end!=b) t->retry_after=v>30?30:(unsigned)v;
    }
    return n;
}
static bool pause_retry(ApiClient *a,unsigned seconds) {
    activity(a,"Transient failure: waiting before bounded retry (Esc cancels)...");
    struct timespec tick={0,100000000};
    for (unsigned i=0;i<seconds*10;i++) { if (cancelled(a)) return false; nanosleep(&tick,NULL); }
    return !cancelled(a);
}
static CURLcode perform(ApiClient *a) {
    CURLM *m=curl_multi_init(); if (!m) return CURLE_OUT_OF_MEMORY;
    CURL *easy=a->handle; CURLcode result=CURLE_RECV_ERROR; int running=0;
    if (curl_multi_add_handle(m,easy)!=CURLM_OK) { curl_multi_cleanup(m); return CURLE_FAILED_INIT; }
    CURLMcode mc=curl_multi_perform(m,&running);
    while (mc==CURLM_OK && running && !cancelled(a)) {
        mc=curl_multi_poll(m,NULL,0,100,NULL);
        if (mc==CURLM_OK) mc=curl_multi_perform(m,&running);
    }
    if (cancelled(a)) result=CURLE_ABORTED_BY_CALLBACK;
    else if (mc==CURLM_OK) {
        int left; CURLMsg *msg;
        while ((msg=curl_multi_info_read(m,&left))) if (msg->msg==CURLMSG_DONE) result=msg->data.result;
    }
    curl_multi_remove_handle(m,easy); curl_multi_cleanup(m); return result;
}
static bool transient_curl(CURLcode c) {
    return c==CURLE_COULDNT_CONNECT||c==CURLE_COULDNT_RESOLVE_HOST||c==CURLE_COULDNT_RESOLVE_PROXY||c==CURLE_OPERATION_TIMEDOUT||c==CURLE_SEND_ERROR||c==CURLE_RECV_ERROR||c==CURLE_PARTIAL_FILE||c==CURLE_GOT_NOTHING||c==CURLE_HTTP2||c==CURLE_HTTP2_STREAM;
}
static const char *status_reason(long code) {
    switch (code) {
    case 400: return "invalid request or unsupported Decisions model";
    case 401: return "API authentication failed; check OPENROUTER_API_KEY";
    case 402: return "insufficient OpenRouter credits";
    case 403: return "access denied by OpenRouter or provider";
    case 404: return "model or Decisions endpoint not found";
    case 413: return "request too large for the provider";
    case 422: return "model does not accept these Decisions parameters";
    case 429: return "rate limit exceeded";
    default: return "OpenRouter request failed";
    }
}
bool api_decide(void *ctx,const json_t *request,double *probabilities,char *error) {
    ApiClient *a=ctx; CURL *easy=a->handle;
    char *payload=json_dumps(request,JSON_COMPACT|JSON_ENCODE_ANY);
    if (!payload) { error_set(error,"Cannot allocate Decisions request."); return false; }
    if (strlen(payload)>LY_LIMIT) { free(payload); error_set(error,"Request exceeds 16 MiB limit; shorten input line/context."); return false; }
    size_t auth_len=strlen(a->key)+24; char *auth=malloc(auth_len);
    if (!auth) { free(payload); error_set(error,"Out of memory building authentication header."); return false; }
    snprintf(auth,auth_len,"Authorization: Bearer %s",a->key);
    struct curl_slist *headers=NULL,*next;
    const char *values[]={auth,"Content-Type: application/json","Accept: application/json","X-Title: LYRCENSOR"};
    for (size_t i=0;i<sizeof(values)/sizeof(values[0]);i++) {
        next=curl_slist_append(headers,values[i]);
        if (!next) { curl_slist_free_all(headers); free(auth); free(payload); error_set(error,"Out of memory building HTTP headers."); return false; }
        headers=next;
    }
    free(auth); bool ok=false;
    for (unsigned attempt=0;attempt<3;attempt++) {
        if (cancelled(a)) { error_set(error,"Processing cancelled."); break; }
        Transfer t={.api=a}; curl_easy_reset(easy);
#define OPT(k,v) do { if (curl_easy_setopt(easy,(k),(v))!=CURLE_OK) { error_set(error,"Cannot configure HTTPS client."); goto cleanup; } } while (0)
#ifdef LY_TEST_HTTP
        OPT(CURLOPT_URL,a->test_url?a->test_url:"https://openrouter.ai/api/alpha/decisions");
        OPT(CURLOPT_PROTOCOLS_STR,a->test_url?"http":"https");
#else
        OPT(CURLOPT_URL,"https://openrouter.ai/api/alpha/decisions");
        OPT(CURLOPT_PROTOCOLS_STR,"https");
#endif
        OPT(CURLOPT_REDIR_PROTOCOLS_STR,"https");
        OPT(CURLOPT_HTTPHEADER,headers); OPT(CURLOPT_POST,1L);
        OPT(CURLOPT_POSTFIELDS,payload); OPT(CURLOPT_POSTFIELDSIZE_LARGE,(curl_off_t)strlen(payload));
        OPT(CURLOPT_SSL_VERIFYPEER,1L); OPT(CURLOPT_SSL_VERIFYHOST,2L);
        OPT(CURLOPT_TIMEOUT,a->config->timeout); OPT(CURLOPT_CONNECTTIMEOUT,15L);
        OPT(CURLOPT_NOSIGNAL,1L); OPT(CURLOPT_FOLLOWLOCATION,0L);
        OPT(CURLOPT_WRITEFUNCTION,receive); OPT(CURLOPT_WRITEDATA,&t);
        OPT(CURLOPT_HEADERFUNCTION,header); OPT(CURLOPT_HEADERDATA,&t);
        OPT(CURLOPT_ACCEPT_ENCODING,""); OPT(CURLOPT_USERAGENT,"LYRCENSOR/1.0");
#undef OPT
        a->progress->requests_started++; activity(a,"Submitting native noul questions to OpenRouter...");
        CURLcode c=perform(a); long status=0; char *content_type=NULL;
        curl_easy_getinfo(easy,CURLINFO_RESPONSE_CODE,&status);
        curl_easy_getinfo(easy,CURLINFO_CONTENT_TYPE,&content_type);
        bool retry=false;
        if (cancelled(a)) error_set(error,"Processing cancelled.");
        else if (c!=CURLE_OK) {
            if (t.overflow) error_set(error,"API response exceeded memory/size limit.");
            else error_set(error,"HTTPS failure: %s.",curl_easy_strerror(c));
            retry=!t.overflow&&transient_curl(c);
        } else {
            a->progress->requests_completed++;
            if (status<200||status>=300) { error_set(error,"HTTP %ld: %s.",status,status_reason(status)); retry=api_retry_status(status); }
            else if (!content_type || strncasecmp(content_type,"application/json",16) || (content_type[16] && content_type[16]!=';' && content_type[16]!=' ')) {
                error_set(error,"Unexpected response content type; Decisions requires application/json.");
            } else {
                json_t *r=response_parse(t.body.data?t.body.data:"",t.body.len,error);
                if (r) {
                    usage_add(&a->progress->usage,r);
                    ok=response_validate(r,json_object_get(request,"questions"),probabilities,error);
                    /* Explicit API errors/refusals are not retried as malformed decisions. */
                    retry=!ok&&!json_object_get(r,"error"); json_decref(r);
                } else retry=true;
            }
        }
        unsigned delay=t.retry_after>(1u<<attempt)?t.retry_after:(1u<<attempt);
        buf_free(&t.body);
        if (ok) { activity(a,"Response received and all question IDs/probabilities validated."); break; }
        activity(a,error);
        if (!retry||attempt==2||cancelled(a)||!pause_retry(a,delay)) break;
    }
cleanup:
    curl_easy_setopt(easy,CURLOPT_HTTPHEADER,NULL);
    curl_easy_setopt(easy,CURLOPT_POSTFIELDS,NULL);
    curl_slist_free_all(headers); free(payload);
    if (!ok&&cancelled(a)) error_set(error,"Processing cancelled.");
    return ok;
}
