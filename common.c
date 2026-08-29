#include "common.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

log_level g_log_level = LOG_OFF;

static void (*log_sink)(log_level lvl, const char *msg);

void byte_ring_init(byte_ring *r, uint8_t *buf, uint32_t cap)
{
    r->buf = buf;
    r->cap = cap;
    r->head = 0;
    r->tail = 0;
}

bool byte_ring_push(byte_ring *r, uint8_t b)
{
    if (r->head - r->tail == r->cap)
        return false;
    r->buf[r->head & (r->cap - 1)] = b;
    r->head++;
    return true;
}

bool byte_ring_pop(byte_ring *r, uint8_t *out)
{
    if (r->head == r->tail)
        return false;
    *out = r->buf[r->tail & (r->cap - 1)];
    r->tail++;
    return true;
}

uint32_t byte_ring_len(const byte_ring *r)
{
    return r->head - r->tail;
}

void log_set_sink(void (*sink)(log_level lvl, const char *msg))
{
    log_sink = sink;
}

void log_emit(log_level lvl, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    if (!log_sink)
        return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    log_sink(lvl, buf);
}

uint64_t monotonic_ns(void)
{
#ifdef __APPLE__
    /* clock_gettime(CLOCK_MONOTONIC) here detours through gettimeofday */
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}
