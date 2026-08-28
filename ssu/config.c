/* config.c — session config (config.rs) + --comm1/--comm2 grammar
 * (config_parser.rs). Tokenizer is shellish_parse-alike: whitespace split,
 * '...' literal, "..." with backslash escapes, backslash escapes outside
 * quotes, unclosed quote / dangling backslash = error. */
#include "ssu/ssu.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

#if defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
static int fail(char *err, size_t err_len, const char *fmt, ...)
{
    if (err && err_len) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, err_len, fmt, ap);
        va_end(ap);
    }
    return -1;
}

/* ---- tokenizer ---- */

struct tokbuf { char *s; size_t len, cap; };
struct tokens { char **v; size_t n, cap; };

static int tokbuf_push(struct tokbuf *b, char c)
{
    if (b->len + 1 >= b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 32;
        char *ns = realloc(b->s, ncap);
        if (!ns)
            return -1;
        b->s = ns;
        b->cap = ncap;
    }
    b->s[b->len++] = c;
    return 0;
}

static int tokens_emit(struct tokens *t, struct tokbuf *b)
{
    if (t->n == t->cap) {
        size_t ncap = t->cap ? t->cap * 2 : 8;
        char **nv = realloc(t->v, ncap * sizeof *nv);
        if (!nv)
            return -1;
        t->v = nv;
        t->cap = ncap;
    }
    char *s = malloc(b->len + 1);
    if (!s)
        return -1;
    if (b->len)
        memcpy(s, b->s, b->len);
    s[b->len] = 0;
    t->v[t->n++] = s;
    b->len = 0;
    return 0;
}

static void tokens_free(struct tokens *t)
{
    for (size_t i = 0; i < t->n; i++)
        free(t->v[i]);
    free(t->v);
    t->v = NULL;
    t->n = t->cap = 0;
}

static int tokenize(const char *s, struct tokens *toks, const char **errmsg)
{
    struct tokbuf buf = {0};
    bool have = false;
    int rc = -1;

    for (size_t i = 0; s[i]; i++) {
        char c = s[i];
        if (c == '\'') {
            have = true;
            for (i++; s[i] != '\''; i++) {
                if (!s[i]) {
                    *errmsg = "Invalid argument";
                    goto out;
                }
                if (tokbuf_push(&buf, s[i]))
                    goto oom;
            }
            continue;
        }
        if (c == '"') {
            have = true;
            for (i++; s[i] != '"'; i++) {
                if (!s[i]) {
                    *errmsg = "Invalid argument";
                    goto out;
                }
                if (s[i] == '\\') {
                    i++;
                    if (!s[i]) {
                        *errmsg = "Invalid argument";
                        goto out;
                    }
                }
                if (tokbuf_push(&buf, s[i]))
                    goto oom;
            }
            continue;
        }
        if (c == '\\') {
            i++;
            if (!s[i]) {
                *errmsg = "Invalid argument";
                goto out;
            }
            have = true;
            if (tokbuf_push(&buf, s[i]))
                goto oom;
            continue;
        }
        if (isspace((unsigned char)c)) {
            if (have && tokens_emit(toks, &buf))
                goto oom;
            have = false;
            continue;
        }
        have = true;
        if (tokbuf_push(&buf, c))
            goto oom;
    }
    if (have && tokens_emit(toks, &buf))
        goto oom;
    rc = 0;
    goto out;
oom:
    *errmsg = "out of memory";
out:
    free(buf.s);
    if (rc)
        tokens_free(toks);
    return rc;
}

/* ---- subcommand parsing (clap grammar) ---- */

/* Collects positionals; "--" escapes later flag-looking tokens (clap default). */
static int positionals(const struct tokens *t, const char **pos, size_t max,
                       size_t *npos, char *err, size_t err_len)
{
    size_t n = 0;
    bool raw = false;

    for (size_t i = 1; i < t->n; i++) {
        const char *tok = t->v[i];
        if (!raw && !strcmp(tok, "--")) {
            raw = true;
            continue;
        }
        if ((!raw && tok[0] == '-' && tok[1]) || n == max)
            return fail(err, err_len,
                        "Invalid session configuration: unexpected argument '%s'", tok);
        pos[n++] = tok;
    }
    *npos = n;
    return 0;
}

static int parse_u16_nonzero(const char *s, uint16_t *out)
{
    if (*s == '+')
        s++;
    if (!*s)
        return -1;
    unsigned long v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return -1;
        v = v * 10 + (unsigned long)(*s - '0');
        if (v > 0xFFFF)
            return -1;
    }
    if (!v)
        return -1;
    *out = (uint16_t)v;
    return 0;
}

static int parse_loopback(const struct tokens *t, session_config *out,
                          char *err, size_t err_len)
{
    const char *pos[1];
    size_t npos;
    if (positionals(t, pos, 1, &npos, err, err_len))
        return -1;
    char *initial = dup_str(npos ? pos[0] : "");
    if (!initial)
        return fail(err, err_len, "out of memory");
    out->kind = SESSION_CFG_LOOPBACK;
    out->u.loopback.initial = initial;
    return 0;
}

/* Manifest decision: 1 path -> Pipe, 3 paths -> Pipes with read_write ignored
 * (Rust's positional grammar only ever reaches Pipe with the first path). */
static int parse_pipe(const struct tokens *t, session_config *out,
                      char *err, size_t err_len)
{
    const char *pos[3];
    size_t npos;
    if (positionals(t, pos, 3, &npos, err, err_len))
        return -1;
    if (npos == 1) {
        char *path = dup_str(pos[0]);
        if (!path)
            return fail(err, err_len, "out of memory");
        out->kind = SESSION_CFG_PIPE;
        out->u.pipe.path = path;
        return 0;
    }
    if (npos == 3) {
        char *rx = dup_str(pos[1]);
        char *tx = dup_str(pos[2]);
        if (!rx || !tx) {
            free(rx);
            free(tx);
            return fail(err, err_len, "out of memory");
        }
        out->kind = SESSION_CFG_PIPES;
        out->u.pipes.rx_path = rx;
        out->u.pipes.tx_path = tx;
        return 0;
    }
    return fail(err, err_len,
                "Invalid session configuration: 'pipe' takes <PATH> or <RW> <READ> <WRITE>");
}

static int parse_exec(const struct tokens *t, session_config *out,
                      char *err, size_t err_len)
{
    const char *cmd = NULL, *rows_s = NULL, *cols_s = NULL;
    bool no_pty = false, raw = false;

    for (size_t i = 1; i < t->n; i++) {
        const char *tok = t->v[i];
        if (!raw && !strcmp(tok, "--")) {
            raw = true;
            continue;
        }
        if (!raw && tok[0] == '-' && tok[1]) {
            if (!strcmp(tok, "--no-pty")) {
                if (no_pty)
                    return fail(err, err_len,
                                "Invalid session configuration: '--no-pty' cannot be used multiple times");
                no_pty = true;
                continue;
            }
            const char **slot;
            const char *name;
            if (!strncmp(tok, "--rows", 6) && (!tok[6] || tok[6] == '=')) {
                slot = &rows_s;
                name = "--rows";
            } else if (!strncmp(tok, "--cols", 6) && (!tok[6] || tok[6] == '=')) {
                slot = &cols_s;
                name = "--cols";
            } else {
                return fail(err, err_len,
                            "Invalid session configuration: unexpected argument '%s'", tok);
            }
            if (*slot)
                return fail(err, err_len,
                            "Invalid session configuration: '%s' cannot be used multiple times", name);
            if (tok[6] == '=') {
                *slot = tok + 7;
                continue;
            }
            if (i + 1 == t->n)
                return fail(err, err_len,
                            "Invalid session configuration: a value is required for '%s'", name);
            *slot = t->v[++i];
            continue;
        }
        if (cmd)
            return fail(err, err_len,
                        "Invalid session configuration: unexpected argument '%s'", tok);
        cmd = tok;
    }

    if (!cmd)
        return fail(err, err_len,
                    "Invalid session configuration: missing <COMMAND> for 'exec'");
    if (no_pty && (rows_s || cols_s))
        return fail(err, err_len,
                    "Invalid session configuration: '--rows'/'--cols' cannot be used with '--no-pty'");

    if (no_pty) {
        char *command = dup_str(cmd);
        if (!command)
            return fail(err, err_len, "out of memory");
        out->kind = SESSION_CFG_EXEC;
        out->u.exec.command = command;
        return 0;
    }

    uint16_t rows = 24, cols = 80;
    if (rows_s && parse_u16_nonzero(rows_s, &rows))
        return fail(err, err_len,
                    "Invalid session configuration: invalid value '%s' for '--rows'", rows_s);
    if (cols_s && parse_u16_nonzero(cols_s, &cols))
        return fail(err, err_len,
                    "Invalid session configuration: invalid value '%s' for '--cols'", cols_s);
    char *c = dup_str(cmd);
    if (!c)
        return fail(err, err_len, "out of memory");
    out->kind = SESSION_CFG_EXEC_PTY;
    out->u.exec_pty.cmd = c;
    out->u.exec_pty.rows = rows;
    out->u.exec_pty.cols = cols;
    return 0;
}

static int parse_serial(const struct tokens *t, session_config *out,
                        char *err, size_t err_len)
{
    const char *pos[1];
    size_t npos;

    if (positionals(t, pos, 1, &npos, err, err_len))
        return -1;
    if (npos != 1)
        return fail(err, err_len,
                    "Invalid session configuration: 'serial' takes <PATH>");
    char *path = dup_str(pos[0]);
    if (!path)
        return fail(err, err_len, "out of memory");
    out->kind = SESSION_CFG_SERIAL;
    out->u.serial.path = path;
    return 0;
}

/* ---- public API ---- */

void session_config_default(session_config *out)
{
    memset(out, 0, sizeof *out);
    out->kind = SESSION_CFG_LOOPBACK;
    out->u.loopback.initial = dup_str("");
}

int session_config_parse(const char *s, session_config *out,
                         char *err, size_t err_len)
{
    memset(out, 0, sizeof *out);

    struct tokens toks = {0};
    const char *tokerr = NULL;
    if (tokenize(s, &toks, &tokerr))
        return fail(err, err_len, "%s", tokerr);

    int rc;
    if (toks.n == 0)
        rc = fail(err, err_len, "Invalid session configuration: missing subcommand");
    else if (!strcmp(toks.v[0], "loopback"))
        rc = parse_loopback(&toks, out, err, err_len);
    else if (!strcmp(toks.v[0], "pipe"))
        rc = parse_pipe(&toks, out, err, err_len);
    else if (!strcmp(toks.v[0], "exec"))
        rc = parse_exec(&toks, out, err, err_len);
    else if (!strcmp(toks.v[0], "serial"))
        rc = parse_serial(&toks, out, err, err_len);
    else
        rc = fail(err, err_len,
                  "Invalid session configuration: unrecognized subcommand '%s'", toks.v[0]);
    tokens_free(&toks);
    return rc;
}

void session_config_free(session_config *cfg)
{
    if (!cfg)
        return;
    switch (cfg->kind) {
    case SESSION_CFG_LOOPBACK: free(cfg->u.loopback.initial); break;
    case SESSION_CFG_PIPE:     free(cfg->u.pipe.path); break;
    case SESSION_CFG_PIPES:    free(cfg->u.pipes.rx_path); free(cfg->u.pipes.tx_path); break;
    case SESSION_CFG_EXEC:     free(cfg->u.exec.command); break;
    case SESSION_CFG_EXEC_PTY: free(cfg->u.exec_pty.cmd); break;
    case SESSION_CFG_SERIAL:   free(cfg->u.serial.path); break;
    }
    memset(cfg, 0, sizeof *cfg);
}
