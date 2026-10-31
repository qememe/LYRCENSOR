#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
void config_init(Config *c) {
    *c=(Config){.timeout=120,.line_threshold=0.20,.token_threshold=0.75};
    const char *m=getenv("OPENROUTER_MODEL"); if (!m || !*m) m="typesafe/jev-1.13";
    if (strlen(m)<=LY_MODEL_MAX) memcpy(c->model,m,strlen(m)+1);
}
bool config_valid(const Config *c, char *error) {
    size_t n=strlen(c->model);
    if (!n) { error_set(error,"Set an OpenRouter Decisions model ID in F2 Settings."); return false; }
    for (size_t i=0;i<n;i++) {
        unsigned char v=(unsigned char)c->model[i];
        if (!((v>='a'&&v<='z')||(v>='A'&&v<='Z')||(v>='0'&&v<='9')||strchr("/-_.:",v))) {
            error_set(error,"Model ID contains invalid characters."); return false;
        }
    }
    if (c->timeout<5||c->timeout>3600) { error_set(error,"Timeout must be between 5 and 3600 seconds."); return false; }
    if (!isfinite(c->line_threshold)||!isfinite(c->token_threshold)||c->line_threshold<=0||c->line_threshold>1||c->token_threshold<=0||c->token_threshold>1) {
        error_set(error,"Thresholds must be greater than 0 and at most 1."); return false;
    }
    return true;
}
bool config_key_valid(const char *key) {
    if (!key || !*key || strlen(key)>4096) return false;
    for (const unsigned char *p=(const unsigned char *)key;*p;p++) if (*p<=32||*p>=127) return false;
    return true;
}
