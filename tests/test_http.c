#include "openrouter.h"
#include "support.h"
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char good[]="{\"model\":\"typesafe/jev-1.13\",\"answers\":{\"line_0\":{\"type\":\"noul\",\"noul\":0.2}},\"usage\":{\"input_tokens\":10,\"output_tokens\":1,\"cost\":0.001}}";
static void run(const Fixture *fixtures,size_t count,bool expected,size_t completed,size_t reports) {
    Server server=server_start(fixtures,count); Config cfg; config_init(&cfg); strcpy(cfg.model,"typesafe/jev-1.13"); cfg.timeout=5;
    Progress progress={0}; atomic_bool cancel; atomic_init(&cancel,false);
    ApiClient client={.config=&cfg,.key="fixture-only",.cancel=&cancel,.progress=&progress,.test_url=server.url}; char error[LY_ERROR];
    T(api_init(&client,error)); json_t *req=json_pack("{s:s,s:s,s:{s:{s:s,s:s}}}","model",cfg.model,"state","untrusted lyrics","questions","line_0","type","noul","instructions","review line 0"); T(req);
    double p=-1; bool ok=api_decide(&client,req,&p,error);
    if (ok!=expected) fprintf(stderr,"HTTP test unexpected outcome: %s\n",error);
    T(ok==expected); T(progress.requests_started==count); T(progress.requests_completed==completed); T(progress.usage.cost_reports==reports);
    if (ok) T(p==0.2); else T(!strstr(error,"fixture-only"));
    api_cleanup(&client); json_decref(req); server_join(server);
}
static void *cancel_soon(void *ctx) { test_sleep(150); atomic_store((atomic_bool *)ctx,true); return NULL; }
static void test_cancel(void) {
    Fixture f={.status=200,.body=good,.delay_ms=1000}; Server server=server_start(&f,1);
    Config cfg; config_init(&cfg); strcpy(cfg.model,"typesafe/jev-1.13"); Progress p={0}; atomic_bool cancel; atomic_init(&cancel,false);
    ApiClient c={.config=&cfg,.key="fixture-only",.cancel=&cancel,.progress=&p,.test_url=server.url}; char error[LY_ERROR]; T(api_init(&c,error));
    json_t *req=json_pack("{s:s,s:s,s:{s:{s:s,s:s}}}","model",cfg.model,"state","data","questions","line_0","type","noul","instructions","review"); T(req);
    pthread_t t; T(pthread_create(&t,NULL,cancel_soon,&cancel)==0); double value=0;
    T(!api_decide(&c,req,&value,error)); T(strstr(error,"cancelled")); T(p.requests_started==1&&p.requests_completed==0);
    pthread_join(t,NULL); json_decref(req); api_cleanup(&c); server_join(server);
}
int main(void) {
    unsetenv("OPENROUTER_MODEL"); setenv("NO_PROXY","127.0.0.1",1); T(curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK);
    Fixture retry[]={{.status=429,.body="{}"},{.status=200,.body=good}}; run(retry,2,true,2,1);
    Fixture invalid[]={{.status=200,.body="{\"answers\":{}}"},{.status=200,.body=good}}; run(invalid,2,true,2,1);
    Fixture duplicate[]={{.status=200,.body="{\"answers\":{\"line_0\":{},\"line_0\":{}}}"},{.status=200,.body=good}}; run(duplicate,2,true,2,1);
    int codes[]={400,401,402,403,404,413,422};
    for (size_t i=0;i<sizeof(codes)/sizeof(codes[0]);i++) { Fixture f={.status=codes[i],.body="{\"error\":{\"message\":\"fixture-only\"}}"}; run(&f,1,false,1,0); }
    Fixture html={.status=200,.body=good,.type="text/html"}; run(&html,1,false,1,0);
    Fixture refusal={.status=200,.body="{\"error\":{\"message\":\"refused\"}}"}; run(&refusal,1,false,1,0);
    Fixture transient[3]; for (size_t i=0;i<3;i++) transient[i]=(Fixture){.status=503,.body="{}"}; run(transient,3,false,3,0);
    Fixture partial[3]; for (size_t i=0;i<3;i++) partial[i]=(Fixture){.status=200,.body=good,.truncate=true}; run(partial,3,false,0,0);
    Fixture missing[3]; for (size_t i=0;i<3;i++) missing[i]=(Fixture){.status=200,.body="{\"answers\":{}}"}; run(missing,3,false,3,0);
    test_cancel(); curl_global_cleanup(); puts("PASS: real libcurl transport against loopback HTTP fixtures, errors, bounded retries, partial responses, cancellation."); return 0;
}
