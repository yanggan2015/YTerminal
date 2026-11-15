#include "log_ring.h"
#include <stdlib.h>
#include <string.h>

struct YtRing {
    char *buf;
    int cap;
    int head;
    unsigned long long total;
    CRITICAL_SECTION lock;
};

YtRing *yt_ring_create(int cap)
{
    YtRing *r;
    if (cap < 4096) cap = 4096;
    r = (YtRing *)calloc(1, sizeof(YtRing));
    if (!r) return NULL;
    r->buf = (char *)malloc((size_t)cap);
    if (!r->buf) { free(r); return NULL; }
    r->cap = cap;
    InitializeCriticalSection(&r->lock);
    return r;
}

void yt_ring_destroy(YtRing *r)
{
    if (!r) return;
    DeleteCriticalSection(&r->lock);
    free(r->buf);
    free(r);
}

void yt_ring_append(YtRing *r, const char *data, int n)
{
    int i;
    if (!r || !data || n <= 0) return;
    EnterCriticalSection(&r->lock);
    for (i = 0; i < n; i++) {
        r->buf[r->head] = data[i];
        r->head = (r->head + 1) % r->cap;
        r->total++;
    }
    LeaveCriticalSection(&r->lock);
}

unsigned long long yt_ring_cursor(YtRing *r)
{
    unsigned long long t;
    if (!r) return 0;
    EnterCriticalSection(&r->lock);
    t = r->total;
    LeaveCriticalSection(&r->lock);
    return t;
}

int yt_ring_read_since(YtRing *r, unsigned long long since, char *out, int cap,
                       unsigned long long *out_since)
{
    unsigned long long avail, start, i;
    int n = 0;
    if (!r || !out || cap <= 0) return 0;
    EnterCriticalSection(&r->lock);
    {
        unsigned long long oldest = r->total > (unsigned long long)r->cap
                                    ? r->total - (unsigned long long)r->cap : 0;
        if (since < oldest) since = oldest;
        if (since > r->total) since = r->total;
    }
    avail = r->total - since;
    if (avail > (unsigned long long)(cap - 1)) avail = (unsigned long long)(cap - 1);
    start = since;
    for (i = 0; i < avail; i++) {
        unsigned long long abs = start + i;
        int idx = (int)(((long long)r->head + (long long)(abs - r->total)) % r->cap);
        if (idx < 0) idx += r->cap;
        out[n++] = r->buf[idx];
    }
    out[n] = 0;
    if (out_since) *out_since = start + avail;
    LeaveCriticalSection(&r->lock);
    return n;
}
