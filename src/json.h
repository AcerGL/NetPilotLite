#ifndef NP_JSON_H
#define NP_JSON_H
#include "lite.h"
/* Bounded strict JSON. Tokens are pre-order; next skips the entire subtree. */
typedef struct NP_JSON_TOKEN {
    size_t start, end;
    int next, count;
    char type;
} NP_JSON_TOKEN;
typedef struct NP_JSON {
    const char *text;
    size_t size, pos;
    int used;
    NP_JSON_TOKEN *tokens;
} NP_JSON;
BOOL np_json_parse(NP_JSON *, const char *, size_t);
void np_json_free(NP_JSON *);
int np_json_field(const NP_JSON *, int, const char *);
BOOL np_json_string(const NP_JSON *, int, WCHAR *, size_t);
BOOL np_json_uint(const NP_JSON *, int, ULONGLONG *);
BOOL np_json_false(const NP_JSON *, int);
#endif
