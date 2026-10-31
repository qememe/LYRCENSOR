#include "response.h"
#include <math.h>
#include <limits.h>
#include <string.h>
json_t *response_parse(const char *s, size_t n, char *error) {
    json_error_t e; json_t *j=json_loadb(s,n,JSON_REJECT_DUPLICATES,&e);
    if (!json_is_object(j)) { json_decref(j); error_set(error,"Invalid Decisions JSON response (object required; duplicate keys forbidden)."); return NULL; }
    return j;
}
bool response_validate(const json_t *r, const json_t *questions, double *probabilities, char *error) {
    if (json_object_get(r,"error")) { error_set(error,"Decisions API returned an error or refusal."); return false; }
    json_t *a=json_object_get(r,"answers");
    if (!json_is_object(a)||json_object_size(a)!=json_object_size(questions)) goto bad;
    const char *key; json_t *q; size_t i=0;
    json_object_foreach((json_t *)questions,key,q) {
        (void)q; json_t *v=json_object_get(a,key), *type=json_object_get(v,"type"), *p=json_object_get(v,"noul");
        if (!json_is_object(v)||json_object_size(v)!=2||!json_is_string(type)||strcmp(json_string_value(type),"noul")||!json_is_number(p)) goto bad;
        double value=json_number_value(p);
        if (!isfinite(value)||value<0||value>1) goto bad;
    }
    i=0;
    json_object_foreach((json_t *)questions,key,q) {
        (void)q;
        probabilities[i++]=json_number_value(json_object_get(json_object_get(a,key),"noul"));
    }
    return true;
bad: error_set(error,"Invalid/incomplete Decisions answers: exact IDs and finite noul probabilities in [0,1] required."); return false;
}
void usage_add(Usage *u, const json_t *r) {
    json_t *j=json_object_get(r,"usage"), *in=json_object_get(j,"input_tokens"), *out=json_object_get(j,"output_tokens"), *cost=json_object_get(j,"cost");
    if (json_is_integer(in)&&json_is_integer(out)&&json_integer_value(in)>=0&&json_integer_value(out)>=0) {
        unsigned long long a=(unsigned long long)json_integer_value(in),b=(unsigned long long)json_integer_value(out);
        if (a<=ULLONG_MAX-u->input_tokens && b<=ULLONG_MAX-u->output_tokens) { u->input_tokens+=a; u->output_tokens+=b; u->token_reports++; }
    }
    if (json_is_number(cost)&&isfinite(json_number_value(cost))&&json_number_value(cost)>=0&&isfinite(u->cost+json_number_value(cost))) { u->cost+=json_number_value(cost); u->cost_reports++; }
}
