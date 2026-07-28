/* common.h — shared primitives for ezalb, a C port of blaze. */
#ifndef BLAZE_COMMON_H
#define BLAZE_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Single-threaded SPSC byte ring over caller storage; cap must be a power
 * of two. Replaces std::sync::mpsc channels: DUART pipes use cap 16
 * (sync_channel(16)); keyboard queues use cap 256 (Rust unbounded; callers
 * drop + debug-log on full). */
typedef struct byte_ring {
    uint8_t *buf;
    uint32_t cap;
    uint32_t head, tail; /* push at head, pop at tail; len = head - tail */
} byte_ring;

void     byte_ring_init(byte_ring *r, uint8_t *buf, uint32_t cap);
bool     byte_ring_push(byte_ring *r, uint8_t b);   /* false when full */
bool     byte_ring_pop(byte_ring *r, uint8_t *out); /* false when empty */
uint32_t byte_ring_len(const byte_ring *r);

typedef enum log_level {
    LOG_OFF = 0, LOG_ERROR, LOG_WARN, LOG_INFO, LOG_DEBUG, LOG_TRACE
} log_level;

extern log_level g_log_level; /* LOG_OFF until a logging setup fn runs */

#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
void log_emit(log_level lvl, const char *fmt, ...);
/* Sink receives the formatted message (no newline). NULL = discard. */
void log_set_sink(void (*sink)(log_level lvl, const char *msg));

#define LOG_ERRORF(...) do { if (g_log_level >= LOG_ERROR) log_emit(LOG_ERROR, __VA_ARGS__); } while (0)
#define LOG_WARNF(...)  do { if (g_log_level >= LOG_WARN)  log_emit(LOG_WARN,  __VA_ARGS__); } while (0)
#define LOG_INFOF(...)  do { if (g_log_level >= LOG_INFO)  log_emit(LOG_INFO,  __VA_ARGS__); } while (0)
#define LOG_DEBUGF(...) do { if (g_log_level >= LOG_DEBUG) log_emit(LOG_DEBUG, __VA_ARGS__); } while (0)
#define LOG_TRACEF(...) do { if (g_log_level >= LOG_TRACE) log_emit(LOG_TRACE, __VA_ARGS__); } while (0)

uint64_t monotonic_ns(void);

static inline uint8_t popcount8(uint8_t v)
{
    v = (uint8_t)((v & 0x55) + ((v >> 1) & 0x55));
    v = (uint8_t)((v & 0x33) + ((v >> 2) & 0x33));
    return (uint8_t)((v & 0x0F) + (v >> 4));
}

#endif
