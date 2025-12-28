/* Minimal embedded cJSON-compatible implementation for flat JSON objects.
   This is intentionally small, only supporting what the project needs:
   - Parsing flat objects with string and integer values
   - Creating an object and adding string/number members
   - Getting object members, checking types, printing unformatted
   This is NOT a full cJSON implementation but sufficient for the current codepaths.
*/

#include "cJSON.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

enum { CJSON_TYPE_NUMBER = 1, CJSON_TYPE_STRING = 2, CJSON_TYPE_OBJECT = 3 };

static const char *skip_ws(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) p++;
    return p;
}

static char *parse_string(const char **sp) {
    const char *p = *sp;
    if (!p || *p != '"') return NULL;
    p++; /* skip '"' */
    const char *start = p;
    size_t len = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p+1)) { p += 2; len += 1; continue; }
        p++; len++;
    }
    if (*p != '"') return NULL;
    char *out = (char*)malloc(len + 1);
    if (!out) return NULL;
    const char *q = start; char *r = out;
    while (q && *q && *q != '"') {
        if (*q == '\\' && *(q+1)) {
            q++;
            *r++ = *q++;
            continue;
        }
        *r++ = *q++;
    }
    *r = '\0';
    p++; /* skip trailing quote */
    *sp = p;
    return out;
}

static long parse_number(const char **sp) {
    char *endptr;
    long v = strtol(*sp, &endptr, 10);
    *sp = endptr;
    return v;
}

cJSON *cJSON_Parse(const char *value) {
    if (!value) return NULL;
    const char *p = skip_ws(value);
    if (*p != '{') return NULL;
    p++;
    cJSON *root = (cJSON*)malloc(sizeof(cJSON));
    if (!root) return NULL;
    memset(root, 0, sizeof(cJSON));
    root->type = CJSON_TYPE_OBJECT;
    cJSON *last = NULL;

    while (1) {
        p = skip_ws(p);
        if (*p == '}') { p++; break; }
        /* parse key */
        char *key = parse_string(&p);
        if (!key) { cJSON_Delete(root); return NULL; }
        p = skip_ws(p);
        if (*p != ':') { free(key); cJSON_Delete(root); return NULL; }
        p++; p = skip_ws(p);
        /* parse value: either string or number */
        cJSON *node = (cJSON*)malloc(sizeof(cJSON));
        if (!node) { free(key); cJSON_Delete(root); return NULL; }
        memset(node, 0, sizeof(cJSON));
        node->string = key;

        if (*p == '"') {
            char *s = parse_string(&p);
            node->valuestring = s;
            node->type = CJSON_TYPE_STRING;
        } else {
            /* parse as integer number */
            long v = parse_number(&p);
            node->valueint = (int)v;
            node->type = CJSON_TYPE_NUMBER;
        }

        if (!root->child) root->child = node;
        if (last) last->next = node;
        last = node;

        p = skip_ws(p);
        if (*p == ',') { p++; continue; }
        if (*p == '}') { p++; break; }
        /* otherwise invalid */
        break;
    }
    return root;
}

char *cJSON_PrintUnformatted(const cJSON *item) {
    if (!item) return NULL;
    /* only support objects */
    const cJSON *child = item->child;
    size_t cap = 256;
    char *buf = (char*)malloc(cap);
    if (!buf) return NULL;
    size_t len = 0;
    buf[len++] = '{';
    while (child) {
        /* ensure space */
        size_t needed = strlen(child->string) + 32 + (child->valuestring ? strlen(child->valuestring) : 0);
        if (len + needed + 8 > cap) { cap = (len + needed + 8) * 2; buf = (char*)realloc(buf, cap); }
        /* add key */
        buf[len++] = '"';
        strcpy(buf + len, child->string);
        len += strlen(child->string);
        buf[len++] = '"';
        buf[len++] = ':';
        if (child->type == CJSON_TYPE_STRING) {
            buf[len++] = '"';
            /* naive escape of quote and backslash */
            const char *s = child->valuestring ? child->valuestring : "";
            for (const char *c = s; *c; ++c) {
                if (*c == '"' || *c == '\\') {
                    if (len + 2 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
                    buf[len++] = '\\';
                    buf[len++] = *c;
                } else {
                    buf[len++] = *c;
                }
            }
            buf[len++] = '"';
        } else {
            /* number */
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%d", child->valueint);
            if (len + n + 1 > cap) { cap = (len + n + 1) * 2; buf = (char*)realloc(buf, cap); }
            strcpy(buf + len, tmp);
            len += n;
        }
        if (child->next) buf[len++] = ',';
        child = child->next;
    }
    buf[len++] = '}';
    buf[len] = '\0';
    return buf;
}

void cJSON_Delete(cJSON *c) {
    if (!c) return;
    /* free children */
    cJSON *child = c->child;
    while (child) {
        cJSON *next = child->next;
        if (child->valuestring) free(child->valuestring);
        if (child->string) free(child->string);
        free(child);
        child = next;
    }
    free(c);
}

cJSON *cJSON_CreateObject(void) {
    cJSON *o = (cJSON*)malloc(sizeof(cJSON));
    if (!o) return NULL;
    memset(o, 0, sizeof(cJSON));
    o->type = CJSON_TYPE_OBJECT;
    return o;
}

static void append_child(cJSON *object, cJSON *child) {
    if (!object || !child) return;
    if (!object->child) object->child = child;
    else {
        cJSON *it = object->child;
        while (it->next) it = it->next;
        it->next = child;
    }
}

void cJSON_AddStringToObject(cJSON *object, const char *name, const char *s) {
    if (!object || !name) return;
    cJSON *n = (cJSON*)malloc(sizeof(cJSON));
    if (!n) return;
    memset(n, 0, sizeof(cJSON));
    n->type = CJSON_TYPE_STRING;
    n->string = strdup(name);
    n->valuestring = s ? strdup(s) : NULL;
    append_child(object, n);
}

void cJSON_AddNumberToObject(cJSON *object, const char *name, long num) {
    if (!object || !name) return;
    cJSON *n = (cJSON*)malloc(sizeof(cJSON));
    if (!n) return;
    memset(n, 0, sizeof(cJSON));
    n->type = CJSON_TYPE_NUMBER;
    n->string = strdup(name);
    n->valueint = (int)num;
    append_child(object, n);
}

cJSON *cJSON_GetObjectItem(const cJSON * const object, const char * const string) {
    if (!object || !string) return NULL;
    cJSON *child = object->child;
    while (child) {
        if (child->string && strcmp(child->string, string) == 0) return child;
        child = child->next;
    }
    return NULL;
}

int cJSON_IsString(const cJSON * const item) { return item && item->type == CJSON_TYPE_STRING; }
int cJSON_IsNumber(const cJSON * const item) { return item && item->type == CJSON_TYPE_NUMBER; }
