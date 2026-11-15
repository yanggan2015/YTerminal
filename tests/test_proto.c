/* tests/test_proto.c : XMODEM / YMODEM / ZMODEM 回环互通测试
 * 两个线程各跑一端，中间用两个内存管道，验证完整收发与校验。
 *
 * 状态：未通过（WIP）。见 docs/执行记录.md 第 11 节：
 *   COM3 真机上 XMODEM 首块 CRC 与 lrzsz 对不上，回环 4 条同样失败。
 *   修好之前不要把本测试接入 CI 的必过项。
 */
#include "proto.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int g_pass = 0, g_fail = 0;
static void check(const char *what, int ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) g_pass++; else g_fail++;
}

typedef struct {
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
    u8 *buf;
    int cap, n, r;
    int closed;
} Pipe;

static void pipe_init(Pipe *p, int cap) {
    memset(p, 0, sizeof(*p));
    InitializeCriticalSection(&p->cs);
    InitializeConditionVariable(&p->cv);
    p->buf = (u8 *)calloc(1, (size_t)cap);
    p->cap = cap;
}

static void pipe_free(Pipe *p) {
    free(p->buf);
    DeleteCriticalSection(&p->cs);
}

typedef struct { Pipe *in, *out; } End;

static int pipe_put(void *ctx, const u8 *b, int len) {
    Pipe *p = (Pipe *)ctx;
    int wrote = 0;
    EnterCriticalSection(&p->cs);
    while (wrote < len) {
        if (p->n >= p->cap) {
            /* 缓冲区满：等读者腾出空间（最多 2s） */
            if (!SleepConditionVariableCS(&p->cv, &p->cs, 2000)) break;
            continue;
        }
        int room = p->cap - p->n;
        int k = len - wrote < room ? len - wrote : room;
        memcpy(p->buf + p->n, b + wrote, (size_t)k);
        p->n += k;
        wrote += k;
        WakeAllConditionVariable(&p->cv);
    }
    LeaveCriticalSection(&p->cs);
    return wrote == len ? wrote : -1;
}

static int pipe_get(void *ctx, u8 *b, int len, int timeout_ms) {
    Pipe *p = (Pipe *)ctx;
    int got = 0;
    EnterCriticalSection(&p->cs);
    while (p->r == p->n) {
        if (p->closed) break;
        if (!SleepConditionVariableCS(&p->cv, &p->cs, (DWORD)(timeout_ms > 100 ? 100 : timeout_ms))) break;
    }
    if (p->r < p->n) {
        int avail = p->n - p->r;
        got = len < avail ? len : avail;
        memcpy(b, p->buf + p->r, (size_t)got);
        p->r += got;
        WakeAllConditionVariable(&p->cv);
    }
    LeaveCriticalSection(&p->cs);
    return got;
}

static int end_put(void *ctx, const u8 *b, int len) { return pipe_put(((End *)ctx)->out, b, len); }
static int end_get(void *ctx, u8 *b, int len, int ms) { return pipe_get(((End *)ctx)->in, b, len, ms); }

static void prog(void *ctx, const char *name, long done, long total) {
    (void)ctx; (void)name; (void)done; (void)total;
}

typedef int (*SendFn)(YtProtoIO *, const char *, int);
typedef int (*RecvFn)(YtProtoIO *, const char *, int);

typedef struct {
    YtProtoIO io;
    SendFn send;
    const char *path;
    int rc;
} Task;

static DWORD WINAPI send_thread(LPVOID arg) {
    Task *t = (Task *)arg;
    t->rc = t->send(&t->io, t->path, 1);
    return 0;
}

typedef struct {
    YtProtoIO io;
    RecvFn recv;
    const char *path;
    int rc;
} RTask;

static DWORD WINAPI recv_thread(LPVOID arg) {
    RTask *t = (RTask *)arg;
    t->rc = t->recv(&t->io, t->path, 1);
    return 0;
}

static char *read_all(const char *path, size_t *len) {
    return yt_read_file(utf8_to_wide(path), len);
}

static int same_file(const char *a, const char *b) {
    size_t la = 0, lb = 0;
    char *da = read_all(a, &la), *db = read_all(b, &lb);
    int ok = da && db && la == lb && memcmp(da, db, la) == 0;
    free(da); free(db);
    return ok;
}

static void make_src(const char *path, int n) {
    char *b = (char *)malloc((size_t)n);
    for (int i = 0; i < n; i++) b[i] = (char)((i * 37 + 11) & 0xff);
    yt_write_file(utf8_to_wide(path), b, (size_t)n);
    free(b);
}

/* 通用回环：成功返回 1 */
static int loopback(SendFn sf, RecvFn rf, const char *src, const char *out) {
    Pipe a, b;
    pipe_init(&a, 1 << 20);
    pipe_init(&b, 1 << 20);
    End se = { &b, &a };   /* 发送端：从 b 读、往 a 写 */
    End re = { &a, &b };   /* 接收端：从 a 读、往 b 写 */

    Task st;
    RTask rt;
    memset(&st, 0, sizeof(st));
    memset(&rt, 0, sizeof(rt));
    st.io.ctx = &se; st.io.put = end_put; st.io.get = end_get; st.io.progress = prog;
    rt.io.ctx = &re; rt.io.put = end_put; rt.io.get = end_get; rt.io.progress = prog;
    st.send = sf; st.path = src;
    rt.recv = rf; rt.path = out;

    DeleteFileW(utf8_to_wide(out));
    HANDLE hs = CreateThread(NULL, 0, send_thread, &st, 0, NULL);
    HANDLE hr = CreateThread(NULL, 0, recv_thread, &rt, 0, NULL);
    DWORD w = WaitForMultipleObjects(2, (HANDLE[]){ hs, hr }, TRUE, 30000);
    if (w == WAIT_TIMEOUT) {
        a.closed = 1; b.closed = 1;
        WakeAllConditionVariable(&a.cv);
        WakeAllConditionVariable(&b.cv);
        TerminateThread(hs, 0);
        TerminateThread(hr, 0);
    }
    CloseHandle(hs); CloseHandle(hr);
    pipe_free(&a);
    pipe_free(&b);
    if (w == WAIT_TIMEOUT) { printf("    timeout: send=%d recv=%d\n", st.rc, rt.rc); return 0; }
    if (st.rc || rt.rc) printf("    rc: send=%d(%s) recv=%d(%s)\n", st.rc, yt_proto_error(st.rc), rt.rc, yt_proto_error(rt.rc));
    return st.rc == 0 && rt.rc == 0;
}

static int x_send_adapter(YtProtoIO *io, const char *path, int crc) {
    return yt_xmodem_send(io, path, crc);
}
static int y_send_adapter(YtProtoIO *io, const char *path, int crc) {
    (void)crc;
    return yt_ymodem_send(io, path);
}
static int z_send_adapter(YtProtoIO *io, const char *path, int crc) {
    (void)crc;
    return yt_zmodem_send(io, path);
}
static int x_recv_adapter(YtProtoIO *io, const char *path, int crc) {
    return yt_xmodem_recv(io, path, crc);
}
static int y_recv_adapter(YtProtoIO *io, const char *path, int crc) {
    (void)crc;
    return yt_ymodem_recv(io, path);
}
static int z_recv_adapter(YtProtoIO *io, const char *path, int crc) {
    (void)crc;
    return yt_zmodem_recv(io, path);
}

int main(void) {
    printf("== proto loopback ==\n");
    const char *dir = "build\\probe";
    yt_mkdir(utf8_to_wide(dir));

    char src[300], out[300], out2[300], out3[300];
    _snprintf(src, sizeof(src), "%s\\src.bin", dir);
    _snprintf(out, sizeof(out), "%s\\x_recv.bin", dir);
    _snprintf(out2, sizeof(out2), "%s\\y_recv.bin", dir);
    _snprintf(out3, sizeof(out3), "%s\\z_recv.bin", dir);

    make_src(src, 3000);   /* 3 KB，跨多个 1 KB 块 */

    check("XMODEM 1500B 回环", loopback(x_send_adapter, x_recv_adapter, src, out) && same_file(src, out));
    check("YMODEM 1500B 回环", loopback(y_send_adapter, y_recv_adapter, src, out2) && same_file(src, out2));
    check("ZMODEM 1500B 回环", loopback(z_send_adapter, z_recv_adapter, src, out3) && same_file(src, out3));

    /* 空文件 */
    {
        char e[300], eo[300];
        _snprintf(e, sizeof(e), "%s\\empty.bin", dir);
        _snprintf(eo, sizeof(eo), "%s\\empty_x.bin", dir);
        yt_write_file(utf8_to_wide(e), "", 0);
        check("XMODEM 空文件", loopback(x_send_adapter, x_recv_adapter, e, eo));
    }

    printf("\nResult: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
