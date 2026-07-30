/* json.c — minimal JSON parser/writer for the MCP server. */
#include "host/json.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* parser */

typedef struct jp {
    char      *s;
    json_node *pool;
    size_t     cap, used;
} jp;

static json_node *jp_node(jp *p, json_kind kind)
{
    if (p->used >= p->cap)
        return NULL;
    json_node *n = &p->pool[p->used++];
    memset(n, 0, sizeof *n);
    n->kind = kind;
    return n;
}

static void jp_ws(jp *p)
{
    while (*p->s == ' ' || *p->s == '\t' || *p->s == '\n' || *p->s == '\r')
        p->s++;
}

static int hex4(const char *s, unsigned *out)
{
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9')      v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return -1;
    }
    *out = v;
    return 0;
}

static char *utf8_put(char *w, unsigned cp)
{
    if (cp < 0x80) {
        *w++ = (char)cp;
    } else if (cp < 0x800) {
        *w++ = (char)(0xC0 | (cp >> 6));
        *w++ = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        *w++ = (char)(0xE0 | (cp >> 12));
        *w++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *w++ = (char)(0x80 | (cp & 0x3F));
    } else {
        *w++ = (char)(0xF0 | (cp >> 18));
        *w++ = (char)(0x80 | ((cp >> 12) & 0x3F));
        *w++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *w++ = (char)(0x80 | (cp & 0x3F));
    }
    return w;
}

/* Unescapes the quoted string at p->s in place (unescaped form never grows,
 * so writes trail reads). Returns the string start or NULL. */
static char *jp_string(jp *p)
{
    if (*p->s != '"')
        return NULL;
    char *r = ++p->s, *w = r;
    while (*p->s && *p->s != '"') {
        char c = *p->s++;
        if (c != '\\') {
            *w++ = c;
            continue;
        }
        char e = *p->s++;
        switch (e) {
        case '"': case '\\': case '/': *w++ = e; break;
        case 'b': *w++ = '\b'; break;
        case 'f': *w++ = '\f'; break;
        case 'n': *w++ = '\n'; break;
        case 'r': *w++ = '\r'; break;
        case 't': *w++ = '\t'; break;
        case 'u': {
            unsigned cp, lo;
            if (hex4(p->s, &cp) != 0)
                return NULL;
            p->s += 4;
            if (cp >= 0xD800 && cp < 0xDC00 && p->s[0] == '\\' && p->s[1] == 'u' &&
                hex4(p->s + 2, &lo) == 0 && lo >= 0xDC00 && lo < 0xE000) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                p->s += 6;
            }
            w = utf8_put(w, cp);
            break;
        }
        default:
            return NULL;
        }
    }
    if (*p->s != '"')
        return NULL;
    p->s++;
    *w = '\0';
    return r;
}

static json_node *jp_value(jp *p);

static json_node *jp_container(jp *p, json_kind kind, char open, char close)
{
    json_node *o = jp_node(p, kind);
    if (!o)
        return NULL;
    json_node **tail = &o->child;
    p->s++; /* open */
    jp_ws(p);
    if (*p->s == close) {
        p->s++;
        return o;
    }
    for (;;) {
        char *key = NULL;
        jp_ws(p);
        if (kind == J_OBJ) {
            key = jp_string(p);
            if (!key)
                return NULL;
            jp_ws(p);
            if (*p->s++ != ':')
                return NULL;
        }
        json_node *v = jp_value(p);
        if (!v)
            return NULL;
        v->key = key;
        *tail = v;
        tail = &v->next;
        jp_ws(p);
        if (*p->s == ',') {
            p->s++;
            continue;
        }
        if (*p->s == close) {
            p->s++;
            return o;
        }
        return NULL;
    }
    (void)open;
}

static json_node *jp_value(jp *p)
{
    jp_ws(p);
    switch (*p->s) {
    case '{': return jp_container(p, J_OBJ, '{', '}');
    case '[': return jp_container(p, J_ARR, '[', ']');
    case '"': {
        char *s = jp_string(p);
        if (!s)
            return NULL;
        json_node *n = jp_node(p, J_STR);
        if (n)
            n->str = s;
        return n;
    }
    case 't':
        if (strncmp(p->s, "true", 4) != 0)
            return NULL;
        p->s += 4;
        json_node *t = jp_node(p, J_BOOL);
        if (t)
            t->b = true;
        return t;
    case 'f':
        if (strncmp(p->s, "false", 5) != 0)
            return NULL;
        p->s += 5;
        return jp_node(p, J_BOOL);
    case 'n':
        if (strncmp(p->s, "null", 4) != 0)
            return NULL;
        p->s += 4;
        return jp_node(p, J_NULL);
    default: {
        char *end;
        double d = strtod(p->s, &end);
        if (end == p->s)
            return NULL;
        p->s = end;
        json_node *n = jp_node(p, J_NUM);
        if (n)
            n->num = d;
        return n;
    }
    }
}

json_node *json_parse(char *buf, json_node *pool, size_t pool_cap)
{
    jp p = { .s = buf, .pool = pool, .cap = pool_cap };
    json_node *root = jp_value(&p);
    if (!root)
        return NULL;
    jp_ws(&p);
    return *p.s == '\0' ? root : NULL;
}

const json_node *json_get(const json_node *obj, const char *key)
{
    if (!obj || obj->kind != J_OBJ)
        return NULL;
    for (const json_node *n = obj->child; n; n = n->next)
        if (n->key && strcmp(n->key, key) == 0)
            return n;
    return NULL;
}

const char *json_str_of(const json_node *obj, const char *key, const char *def)
{
    const json_node *n = json_get(obj, key);
    return n && n->kind == J_STR ? n->str : def;
}

double json_num_of(const json_node *obj, const char *key, double def)
{
    const json_node *n = json_get(obj, key);
    return n && n->kind == J_NUM ? n->num : def;
}

bool json_bool_of(const json_node *obj, const char *key, bool def)
{
    const json_node *n = json_get(obj, key);
    return n && n->kind == J_BOOL ? n->b : def;
}

/* writer */

static void jb_grow(json_buf *b, size_t need)
{
    if (b->len + need + 1 <= b->cap)
        return;
    size_t cap = b->cap ? b->cap : 4096;
    while (b->len + need + 1 > cap)
        cap *= 2;
    char *p = realloc(b->p, cap);
    if (!p) {
        fprintf(stderr, "json: out of memory\n");
        exit(1);
    }
    b->p = p;
    b->cap = cap;
}

void json_buf_init(json_buf *b)
{
    b->p = NULL;
    b->len = b->cap = 0;
    jb_grow(b, 1);
    b->p[0] = '\0';
}

void json_buf_free(json_buf *b)
{
    free(b->p);
    b->p = NULL;
    b->len = b->cap = 0;
}

void jb_rawn(json_buf *b, const char *s, size_t n)
{
    jb_grow(b, n);
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

void jb_raw(json_buf *b, const char *s)
{
    jb_rawn(b, s, strlen(s));
}

void jb_fmt(json_buf *b, const char *fmt, ...)
{
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return;
    }
    jb_grow(b, (size_t)n);
    vsnprintf(b->p + b->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)n;
}

void jb_strn(json_buf *b, const char *s, size_t n)
{
    jb_rawn(b, "\"", 1);
    size_t run = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c != '"' && c != '\\' && c >= 0x20) {
            run++;
            continue;
        }
        jb_rawn(b, s + i - run, run);
        run = 0;
        switch (c) {
        case '"':  jb_rawn(b, "\\\"", 2); break;
        case '\\': jb_rawn(b, "\\\\", 2); break;
        case '\n': jb_rawn(b, "\\n", 2); break;
        case '\r': jb_rawn(b, "\\r", 2); break;
        case '\t': jb_rawn(b, "\\t", 2); break;
        default:   jb_fmt(b, "\\u%04x", c); break;
        }
    }
    jb_rawn(b, s + n - run, run);
    jb_rawn(b, "\"", 1);
}

void jb_str(json_buf *b, const char *s)
{
    jb_strn(b, s, strlen(s));
}

void jb_base64(json_buf *b, const uint8_t *data, size_t n)
{
    static const char tab[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    jb_grow(b, (n + 2) / 3 * 4);
    char *w = b->p + b->len;
    size_t i = 0;
    for (; i + 3 <= n; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16 | (uint32_t)data[i + 1] << 8 | data[i + 2];
        *w++ = tab[v >> 18];
        *w++ = tab[(v >> 12) & 63];
        *w++ = tab[(v >> 6) & 63];
        *w++ = tab[v & 63];
    }
    if (i < n) {
        uint32_t v = (uint32_t)data[i] << 16;
        bool two = i + 1 < n;
        if (two)
            v |= (uint32_t)data[i + 1] << 8;
        *w++ = tab[v >> 18];
        *w++ = tab[(v >> 12) & 63];
        *w++ = two ? tab[(v >> 6) & 63] : '=';
        *w++ = '=';
    }
    b->len = (size_t)(w - b->p);
    b->p[b->len] = '\0';
}
