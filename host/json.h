/* json.h — minimal JSON for the MCP server: in-place node-pool parser for
 * requests, escape-aware growable buffer writer for responses. */
#ifndef BLAZE_JSON_H
#define BLAZE_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum json_kind {
    J_NULL = 0, J_BOOL, J_NUM, J_STR, J_OBJ, J_ARR
} json_kind;

typedef struct json_node json_node;
struct json_node {
    json_kind   kind;
    const char *key;   /* object member name (unescaped) or NULL */
    bool        b;     /* J_BOOL */
    double      num;   /* J_NUM */
    const char *str;   /* J_STR: unescaped, NUL-terminated, points into buf */
    json_node  *child; /* J_OBJ / J_ARR: first member */
    json_node  *next;  /* next sibling */
};

/* Parses buf IN PLACE (strings are unescaped into the buffer; buf must stay
 * alive as long as the nodes). NULL on syntax error or pool exhaustion. */
json_node *json_parse(char *buf, json_node *pool, size_t pool_cap);

const json_node *json_get(const json_node *obj, const char *key);
const char *json_str_of(const json_node *obj, const char *key, const char *def);
double      json_num_of(const json_node *obj, const char *key, double def);
bool        json_bool_of(const json_node *obj, const char *key, bool def);

/* growable output buffer (writers exit(1) on OOM) */
typedef struct json_buf { char *p; size_t len, cap; } json_buf;

void json_buf_init(json_buf *b);
void json_buf_free(json_buf *b);
void jb_rawn(json_buf *b, const char *s, size_t n); /* verbatim */
void jb_raw(json_buf *b, const char *s);
void jb_fmt(json_buf *b, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
;
void jb_strn(json_buf *b, const char *s, size_t n); /* quoted + escaped */
void jb_str(json_buf *b, const char *s);
void jb_base64(json_buf *b, const uint8_t *data, size_t n);

#endif
