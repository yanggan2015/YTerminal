#ifndef YT_LOG_RING_H
#define YT_LOG_RING_H
#include <windows.h>

typedef struct YtRing YtRing;

YtRing *yt_ring_create(int cap);
void    yt_ring_destroy(YtRing *r);
void    yt_ring_append(YtRing *r, const char *data, int n);
/* 自 since 游标增量读；写出到 out，返回写入字节；*out_since 为新游标 */
int     yt_ring_read_since(YtRing *r, unsigned long long since, char *out, int cap,
                           unsigned long long *out_since);
unsigned long long yt_ring_cursor(YtRing *r);

#endif
