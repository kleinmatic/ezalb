/* chan.c — cross-thread 16-slot channel (mpsc::sync_channel(16) parity) and
 * the loopback wakeable_queue (unbounded, always accepts). Wakers from the
 * Rust sources are no-ops in blaze; only the condvars for pump threads remain. */
#include "ssu/ssu.h"
#include "common.h"

#include <stdlib.h>
#include <string.h>

ssu_chan *ssu_chan_new(void)
{
    ssu_chan *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    pthread_mutex_init(&c->mu, NULL);
    pthread_cond_init(&c->not_empty, NULL);
    pthread_cond_init(&c->not_full, NULL);
    c->refs = 1;
    return c;
}

void ssu_chan_ref(ssu_chan *c)
{
    if (!c)
        return;
    pthread_mutex_lock(&c->mu);
    c->refs++;
    pthread_mutex_unlock(&c->mu);
}

void ssu_chan_unref(ssu_chan *c)
{
    if (!c)
        return;
    pthread_mutex_lock(&c->mu);
    int left = --c->refs;
    pthread_mutex_unlock(&c->mu);
    if (left > 0)
        return;
    pthread_cond_destroy(&c->not_empty);
    pthread_cond_destroy(&c->not_full);
    pthread_mutex_destroy(&c->mu);
    free(c);
}

void ssu_chan_close(ssu_chan *c)
{
    if (!c)
        return;
    pthread_mutex_lock(&c->mu);
    c->closed = true;
    atomic_store_explicit(&c->aclosed, true, memory_order_release);
    pthread_cond_broadcast(&c->not_empty);
    pthread_cond_broadcast(&c->not_full);
    pthread_mutex_unlock(&c->mu);
}

static void chan_push(ssu_chan *c, ssu_chan_elem e)
{
    c->ring[(c->head + c->len) % SSU_CHAN_CAP] = e;
    c->len++;
    atomic_store_explicit(&c->alen, c->len, memory_order_release);
    pthread_cond_signal(&c->not_empty);
}

static void chan_pop(ssu_chan *c, ssu_chan_elem *out)
{
    *out = c->ring[c->head];
    c->head = (c->head + 1) % SSU_CHAN_CAP;
    c->len--;
    atomic_store_explicit(&c->alen, c->len, memory_order_release);
    pthread_cond_signal(&c->not_full);
}

int ssu_chan_try_send(ssu_chan *c, ssu_chan_elem e)
{
    pthread_mutex_lock(&c->mu);
    int r;
    if (c->closed)
        r = -1;
    else if (c->len == SSU_CHAN_CAP)
        r = 0;
    else {
        chan_push(c, e);
        r = 1;
    }
    pthread_mutex_unlock(&c->mu);
    return r;
}

/* Rust parity: in-flight elements are still delivered after close. */
int ssu_chan_try_recv(ssu_chan *c, ssu_chan_elem *out)
{
    if (atomic_load_explicit(&c->alen, memory_order_acquire) == 0 &&
        !atomic_load_explicit(&c->aclosed, memory_order_acquire))
        return 0;
    pthread_mutex_lock(&c->mu);
    int r;
    if (c->len > 0) {
        chan_pop(c, out);
        r = 1;
    } else
        r = c->closed ? -1 : 0;
    pthread_mutex_unlock(&c->mu);
    return r;
}

bool ssu_chan_send(ssu_chan *c, ssu_chan_elem e)
{
    pthread_mutex_lock(&c->mu);
    while (!c->closed && c->len == SSU_CHAN_CAP)
        pthread_cond_wait(&c->not_full, &c->mu);
    bool ok = !c->closed;
    if (ok)
        chan_push(c, e);
    pthread_mutex_unlock(&c->mu);
    return ok;
}

bool ssu_chan_recv(ssu_chan *c, ssu_chan_elem *out)
{
    pthread_mutex_lock(&c->mu);
    while (c->len == 0 && !c->closed)
        pthread_cond_wait(&c->not_empty, &c->mu);
    bool ok = c->len > 0;
    if (ok)
        chan_pop(c, out);
    pthread_mutex_unlock(&c->mu);
    return ok;
}

loopback_queue *loopback_queue_new(void)
{
    loopback_queue *q = calloc(1, sizeof *q);
    if (!q)
        return NULL;
    pthread_mutex_init(&q->mu, NULL);
    q->refs = 1;
    return q;
}

void loopback_queue_ref(loopback_queue *q)
{
    if (!q)
        return;
    pthread_mutex_lock(&q->mu);
    q->refs++;
    pthread_mutex_unlock(&q->mu);
}

void loopback_queue_unref(loopback_queue *q)
{
    if (!q)
        return;
    pthread_mutex_lock(&q->mu);
    int left = --q->refs;
    pthread_mutex_unlock(&q->mu);
    if (left > 0)
        return;
    pthread_mutex_destroy(&q->mu);
    free(q->buf);
    free(q);
}

static bool loopback_grow(loopback_queue *q)
{
    size_t new_cap = q->cap ? q->cap * 2 : 64;
    uint8_t *nb = realloc(q->buf, new_cap);
    if (!nb)
        return false;
    if (q->head + q->len > q->cap) {
        /* wrapped: move the head..cap segment to the end of the new buffer */
        size_t tail_len = q->cap - q->head;
        memmove(nb + new_cap - tail_len, nb + q->head, tail_len);
        q->head = new_cap - tail_len;
    }
    q->buf = nb;
    q->cap = new_cap;
    return true;
}

void loopback_queue_push(loopback_queue *q, uint8_t b)
{
    pthread_mutex_lock(&q->mu);
    if (q->len == q->cap && !loopback_grow(q)) {
        pthread_mutex_unlock(&q->mu);
        LOG_ERRORF("loopback queue: allocation failed, byte dropped");
        return;
    }
    q->buf[(q->head + q->len) % q->cap] = b;
    q->len++;
    pthread_mutex_unlock(&q->mu);
}

bool loopback_queue_pop(loopback_queue *q, uint8_t *out)
{
    pthread_mutex_lock(&q->mu);
    if (q->len == 0) {
        pthread_mutex_unlock(&q->mu);
        return false;
    }
    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    q->len--;
    pthread_mutex_unlock(&q->mu);
    return true;
}
