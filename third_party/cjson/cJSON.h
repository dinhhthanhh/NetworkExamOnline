/* Minimal embedded cJSON-compatible header for the project's limited needs. */
#ifndef CJSON_EMBED_H
#define CJSON_EMBED_H

#include <stdlib.h>

typedef struct cJSON {
    int type;
    char *valuestring;
    int valueint;
    char *string; /* key name for object members */
    struct cJSON *next; /* next sibling */
    struct cJSON *child; /* first child (for objects) */
} cJSON;

/* Parse a JSON string containing a flat object with string/number values. */
cJSON *cJSON_Parse(const char *value);

/* Print a JSON value (unformatted) */
char *cJSON_PrintUnformatted(const cJSON *item);

/* Free a parsed/created cJSON structure */
void cJSON_Delete(cJSON *c);

/* Create a new empty object */
cJSON *cJSON_CreateObject(void);

/* Add helpers for string and number values */
void cJSON_AddStringToObject(cJSON *object, const char *name, const char *s);
void cJSON_AddNumberToObject(cJSON *object, const char *name, long num);

/* Access helpers */
cJSON *cJSON_GetObjectItem(const cJSON * const object, const char * const string);
int cJSON_IsString(const cJSON * const item);
int cJSON_IsNumber(const cJSON * const item);

#endif /* CJSON_EMBED_H */
