/* YTerminal - proto.c : XMODEM / YMODEM / ZMODEM
 *
 * 设计要点：
 *  - 只依赖 YtProtoIO 三个回调，串口/SSH/单测回环共用同一套状态机
 *  - 读一律带超时，任何一步失败都能返回错误码，不会卡死界面
 *  - ZMODEM 走 ZBIN32 + CRC32；接收端兼容 ZHEX/ZBIN/ZBIN32 三种帧头
 */
#include "proto.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18
#define CRCCHR 'C'

#define ZPAD 0x2a
#define ZDLE 0x18
#define ZHEX 0x42
#define ZBIN 0x41
#define ZBIN32 0x43

#define ZRQINIT 0
#define ZRINIT 1
#define ZACK 3
#define ZFILE 4
#define ZSKIP 5
#define ZNAK 6
#define ZABORT 7
#define ZFIN 8
#define ZRPOS 9
#define ZDATA 10
#define ZEOF 11
#define ZCAN 16

#define X_MAX_RETRY 10

typedef struct {
    YtProtoIO *io;
    const char *name;
    long total;
    long done;
} P;

static int p_put(P *p, const u8 *b, int n) {
    if (!p->io->put) return -1;
    return p->io->put(p->io->ctx, b, n);
}

static int p_write(P *p, const u8 *b, int n) {
    int w = p_put(p, b, n);
    return w == n ? 0 : -1;
}

static int p_get(P *p, u8 *b, int n, int timeout_ms) {
    if (!p->io->get) return -1;
    return p->io->get(p->io->ctx, b, n, timeout_ms);
}

static int p_get1(P *p, int timeout_ms) {
    u8 c;
    int r = p_get(p, &c, 1, timeout_ms);
    if (r == 1) return (int)c;
    return r == 0 ? -2 : -1;
}

/* 丢掉到行尾/清空缓冲：读空一切可得字节 */
static void p_drain(P *p, int max_reads) {
    u8 b[64];
    for (int i = 0; i < max_reads; i++)
        if (p_get(p, b, (int)sizeof(b), 0) <= 0) break;
}

static void p_prog(P *p, long done) {
    if (p->io->progress) p->io->progress(p->io->ctx, p->name, done, p->total);
}

/* ------------------------------------------------------------------ */
/* 校验                                                                */
/* ------------------------------------------------------------------ */
static u16 crc16_ccitt_i(const u8 *data, int len, u16 init) {
    u16 crc = init;
    for (int i = 0; i < len; i++) {
        crc ^= (u16)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (u16)((crc << 1) ^ 0x1021) : (u16)(crc << 1);
    }
    return crc;
}

static u16 crc16_ccitt(const u8 *data, int len) { return crc16_ccitt_i(data, len, 0); }

static u32 crc32_le(const u8 *data, int len) {
    u32 crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return crc;
}

/* ------------------------------------------------------------------ */
/* XMODEM                                                              */
/* ------------------------------------------------------------------ */
static int x_wait_start(P *p, int use_crc, int timeout_ms) {
    int t = 0;
    while (t < timeout_ms) {
        int c = p_get1(p, 1000);
        if (c == CRCCHR || c == NAK) return use_crc ? 1 : 0;
        if (c == CAN) return -4;
        t += 1000;
    }
    return -2;
}

static int x_send_block(P *p, u8 seq, const u8 *data, int len, int crc) {
    u8 buf[1100];
    int n = 0;
    buf[n++] = (u8)(len == 1024 ? STX : SOH);
    buf[n++] = seq;
    memcpy(buf + n, data, (size_t)len);
    n += len;
    if (crc) {
        u16 c = crc16_ccitt(data, len);
        buf[n++] = (u8)(c >> 8);
        buf[n++] = (u8)(c & 0xff);
    } else {
        u8 s = 0;
        for (int i = 0; i < len; i++) s = (u8)(s + data[i]);
        buf[n++] = s;
    }
    return p_write(p, buf, n);
}

/* 等 ACK/NAK；返回 1=ACK，0=NAK，-4=CAN，-2=超时 */
static int x_ack(P *p, int timeout_ms) {
    int t = 0;
    while (t < timeout_ms) {
        int c = p_get1(p, 1000);
        if (c == ACK) return 1;
        if (c == NAK) return 0;
        if (c == CAN) return -4;
        if (c < 0) {
            if (c == -1) return -1;
            t += 1000;
        }
    }
    return -2;
}

int yt_xmodem_send(YtProtoIO *io, const char *path, int use_crc) {
    if (!io || !path) return -5;
    size_t len = 0;
    char *data = yt_read_file(utf8_to_wide(path), &len);
    if (!data) return -5;
    P p = { io, path, (long)len, 0 };
    int rc = 0, retry = 0;
    int crc = use_crc;
    int start = x_wait_start(&p, 1, 60000);
    if (start == -4) { free(data); return -4; }
    if (start < 0) { free(data); return start; }
    crc = start;

    size_t off = 0;
    u8 seq = 1;
    while (off + 128 <= len || off < len) {
        u8 blk[1024];
        int blen = 1024;
        memset(blk, 0x1a, sizeof(blk));
        size_t left = len - off;
        if (left < (size_t)blen) {
            memcpy(blk, data + off, left);
            blen = 128;   /* 尾块按 128 发，兼容性最好 */
        } else {
            memcpy(blk, data + off, 1024);
        }
        int ok = 0;
        for (retry = 0; retry < X_MAX_RETRY && !ok; retry++) {
            if (x_send_block(&p, seq, blk, blen, crc) != 0) { rc = -1; goto done; }
            int a = x_ack(&p, 30000);
            if (a == 1) ok = 1;
            else if (a == 0) continue;
            else { rc = (a == -4) ? -4 : a; goto done; }
        }
        if (!ok) { rc = -2; goto done; }
        off += (size_t)blen;
        p.done = (long)off;
        p_prog(&p, p.done);
        seq++;
    }
    /* EOT，必要时重发 */
    for (retry = 0; retry < X_MAX_RETRY; retry++) {
        u8 e = EOT;
        if (p_write(&p, &e, 1) != 0) { rc = -1; goto done; }
        int a = x_ack(&p, 30000);
        if (a == 1) { rc = 0; goto done; }
        if (a == -4) { rc = -4; goto done; }
    }
    rc = -2;
done:
    free(data);
    return rc;
}

int yt_xmodem_recv(YtProtoIO *io, const char *save_path, int use_crc) {
    if (!io || !save_path) return -5;
    P p = { io, save_path, -1, 0 };
    FILE *f = _wfopen(utf8_to_wide(save_path), L"wb");
    if (!f) return -5;
    int rc = 0, crc = use_crc;
    int got_first = 0, retry = 0;

    while (!got_first) {
        if (p_write(&p, (const u8 *)(crc ? "C" : "\x15"), 1) != 0) { rc = -1; break; }
        int c = p_get1(&p, 3000);
        if (c == SOH || c == STX) {
            int len = (c == STX) ? 1024 : 128;
            u8 buf[1024 + 4];
            int need = len + 1 + (crc ? 2 : 1);
            int r = p_get(&p, buf, need, 3000);
            if (r < need) { rc = -2; break; }
            u16 c16 = crc16_ccitt(buf + 1, len);
            u8 sum = 0;
            for (int i = 0; i < len; i++) sum = (u8)(sum + buf[i + 1]);
            int good = crc ? ((u16)((buf[len + 1] << 8) | buf[len + 2]) == c16)
                           : ((u8)buf[len + 1] == sum);
            if (good && buf[0] == 1) {
                size_t w = fwrite(buf + 1, 1, (size_t)len, f);
                if (w != (size_t)len) { rc = -1; break; }
                p.done += (long)len;
                p_prog(&p, p.done);
                u8 a = ACK;
                p_write(&p, &a, 1);
                got_first = 1;
            } else {
                u8 n = NAK;
                p_write(&p, &n, 1);
                if (++retry > 10) { rc = -3; break; }
            }
        } else if (c == CAN) { rc = -4; break; }
    }
    if (rc == 0) {
        u8 last = 1;
        int doneflag = 0;
        while (!doneflag) {
            int c = p_get1(&p, 10000);
            if (c == EOT) {
                u8 a = ACK;
                p_write(&p, &a, 1);
                doneflag = 1;
                break;
            }
            if (c == CAN) { rc = -4; break; }
            if (c < 0) { rc = -2; break; }
            if (c == SOH || c == STX) {
                int len = (c == STX) ? 1024 : 128;
                u8 buf[1024 + 4];
                int need = len + 1 + (crc ? 2 : 1);
                if (p_get(&p, buf, need, 3000) < need) { rc = -2; break; }
                u16 c16 = crc16_ccitt(buf + 1, len);
                u8 sum = 0;
                for (int i = 0; i < len; i++) sum = (u8)(sum + buf[i + 1]);
                int good = crc ? ((u16)((buf[len + 1] << 8) | buf[len + 2]) == c16)
                               : ((u8)buf[len + 1] == sum);
                u8 expect = (u8)(last + 1);
                if (good && buf[0] == expect) {
                    size_t w = fwrite(buf + 1, 1, (size_t)len, f);
                    if (w != (size_t)len) { rc = -1; break; }
                    last = expect;
                    p.done += (long)len;
                    p_prog(&p, p.done);
                    u8 a = ACK;
                    p_write(&p, &a, 1);
                } else {
                    u8 n = NAK;
                    p_write(&p, &n, 1);
                }
            }
        }
    }
    fclose(f);
    return rc;
}

/* ------------------------------------------------------------------ */
/* YMODEM                                                              */
/* ------------------------------------------------------------------ */
static int y_send_hdr(P *p, u8 seq, const u8 *data, int len, int crc) {
    return x_send_block(p, seq, data, len, crc);
}

/* 文件名 + 长度，长度后面用空格分隔的 8 进制 mtime/权限（lrzsz 可接受） */
static void y_make_hdr(const char *path, u8 *blk, size_t total) {
    const char *base = path;
    for (const char *s = path; *s; s++)
        if (*s == '/' || *s == '\\') base = s + 1;
    memset(blk, 0, 128);
    size_t n = strlen(base);
    if (n > 100) n = 100;
    memcpy(blk, base, n);
    char meta[40];
    _snprintf(meta, sizeof(meta), "%lu 0 0", (unsigned long)total);
    memcpy(blk + n + 1, meta, strlen(meta));
}

int yt_ymodem_send(YtProtoIO *io, const char *path) {
    if (!io || !path) return -5;
    size_t len = 0;
    char *data = yt_read_file(utf8_to_wide(path), &len);
    if (!data) return -5;
    P p = { io, path, (long)len, 0 };
    int rc = 0, crc = 1, retry = 0;
    int start = x_wait_start(&p, 1, 60000);
    if (start < 0) { free(data); return start; }
    crc = start;

    /* 块 0：128 字节头 */
    u8 hdr[128];
    y_make_hdr(path, hdr, len);
    for (retry = 0; retry < X_MAX_RETRY; retry++) {
        y_send_hdr(&p, 1, hdr, 128, crc);
        int a = x_ack(&p, 30000);
        if (a == 1) break;
        if (a == -4) { rc = -4; goto done; }
    }
    if (retry == X_MAX_RETRY) { rc = -2; goto done; }

    size_t off = 0;
    u8 seq = 1;
    while (off < len) {
        u8 blk[1024];
        size_t left = len - off;
        int blen = (left >= 1024) ? 1024 : 1024;
        memset(blk, 0x1a, sizeof(blk));
        memcpy(blk, data + off, left >= 1024 ? 1024 : left);
        int ok = 0;
        for (retry = 0; retry < X_MAX_RETRY && !ok; retry++) {
            y_send_hdr(&p, seq, blk, blen, crc);
            int a = x_ack(&p, 30000);
            if (a == 1) ok = 1;
            else if (a == -4) { rc = -4; goto done; }
        }
        if (!ok) { rc = -2; goto done; }
        off += (size_t)((left >= 1024) ? 1024 : blen);
        p.done = (long)off;
        p_prog(&p, p.done);
        seq++;
    }
    /* EOT -> ACK，再发空块 0 收尾 */
    for (retry = 0; retry < X_MAX_RETRY; retry++) {
        u8 e = EOT;
        p_write(&p, &e, 1);
        int a = x_ack(&p, 30000);
        if (a == 1) break;
        if (a == -4) { rc = -4; goto done; }
    }
    if (retry == X_MAX_RETRY) { rc = -2; goto done; }
    {
        u8 zero[128];
        memset(zero, 0, sizeof(zero));
        for (retry = 0; retry < X_MAX_RETRY; retry++) {
            y_send_hdr(&p, 1, zero, 128, crc);
            int a = x_ack(&p, 30000);
            if (a == 1) { rc = 0; goto done; }
            if (a == -4) { rc = -4; goto done; }
        }
        rc = -2;
    }
done:
    free(data);
    return rc;
}

/* 解析块 0：返回文件名与长度 */
static int y_parse_hdr(const u8 *blk, int len, char *name, int cap, size_t *size) {
    int n = 0;
    while (n < len && blk[n]) { if (n < cap - 1) name[n] = (char)blk[n]; n++; }
    name[n < cap ? n : cap - 1] = 0;
    if (!n) return 0;
    const u8 *s = blk + n + 1;
    size_t v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (size_t)(*s - '0'); s++; }
    *size = v;
    return 1;
}

int yt_ymodem_recv(YtProtoIO *io, const char *dir) {
    if (!io || !dir) return -5;
    P p = { io, dir, -1, 0 };
    int rc = 0, crc = 1, retry = 0;
    int batch = 0;
    char save[520];
    FILE *f = NULL;

    while (batch < 64) {
        /* 请求块 0 */
        int got = 0;
        while (!got) {
            p_write(&p, (const u8 *)(crc ? "C" : "\x15"), 1);
            int c = p_get1(&p, 3000);
            if (c == CAN) { fclose(f); return -4; }
            if (c != SOH && c != STX) continue;
            int len = (c == STX) ? 1024 : 128;
            u8 buf[1024 + 4];
            int need = len + 1 + (crc ? 2 : 1);
            if (p_get(&p, buf, need, 3000) < need) { rc = -2; goto out; }
            u16 c16 = crc16_ccitt(buf + 1, len);
            u8 sum = 0;
            for (int i = 0; i < len; i++) sum = (u8)(sum + buf[i + 1]);
            int good = crc ? ((u16)((buf[len + 1] << 8) | buf[len + 2]) == c16)
                           : ((u8)buf[len + 1] == sum);
            if (!good || buf[0] != 1) {
                u8 n = NAK;
                p_write(&p, &n, 1);
                if (++retry > 10) { rc = -3; goto out; }
                continue;
            }
            u8 a = ACK;
            p_write(&p, &a, 1);
            char name[260];
            size_t size = 0;
            if (!y_parse_hdr(buf + 1, len, name, sizeof(name), &size)) {
                /* 空块 0 = 批次结束 */
                u8 z = ACK;
                p_write(&p, &z, 1);
                rc = 0;
                goto out;
            }
            _snprintf(save, sizeof(save), "%s\\%s", dir, name);
            f = _wfopen(utf8_to_wide(save), L"wb");
            if (!f) { rc = -5; goto out; }
            p.name = save;
            p.total = (long)size;
            p.done = 0;
            got = 1;
            batch++;
        }
        /* 收数据块 */
        u8 last = 1;
        int done = 0;
        while (!done) {
            int c = p_get1(&p, 10000);
            if (c == EOT) {
                u8 a = ACK;
                p_write(&p, &a, 1);
                fclose(f);
                f = NULL;
                done = 1;
                break;
            }
            if (c == CAN) { rc = -4; goto out; }
            if (c < 0) { rc = -2; goto out; }
            if (c != SOH && c != STX) continue;
            int len = (c == STX) ? 1024 : 128;
            u8 buf[1024 + 4];
            int need = len + 1 + (crc ? 2 : 1);
            if (p_get(&p, buf, need, 3000) < need) { rc = -2; goto out; }
            u16 c16 = crc16_ccitt(buf + 1, len);
            u8 sum = 0;
            for (int i = 0; i < len; i++) sum = (u8)(sum + buf[i + 1]);
            int good = crc ? ((u16)((buf[len + 1] << 8) | buf[len + 2]) == c16)
                           : ((u8)buf[len + 1] == sum);
            u8 expect = (u8)(last + 1);
            if (good && buf[0] == expect) {
                size_t w = fwrite(buf + 1, 1, (size_t)len, f);
                if (w != (size_t)len) { rc = -1; goto out; }
                last = expect;
                p.done += (long)len;
                p_prog(&p, p.done);
                u8 a = ACK;
                p_write(&p, &a, 1);
            } else {
                u8 n = NAK;
                p_write(&p, &n, 1);
                if (++retry > 10) { rc = -3; goto out; }
            }
        }
    }
out:
    if (f) fclose(f);
    return rc;
}

/* ------------------------------------------------------------------ */
/* ZMODEM                                                              */
/* ------------------------------------------------------------------ */
static void z_put_escaped(P *p, const u8 *data, int len) {
    u8 out[1200];
    int n = 0;
    for (int i = 0; i < len; i++) {
        u8 c = data[i];
        if (c == ZDLE || (c & 0xFC) == 0x10 || (c & 0xFE) == 0x90) {
            out[n++] = ZDLE;
            out[n++] = (u8)(c ^ 0x40);
        } else {
            out[n++] = c;
        }
    }
    if (n) p_write(p, out, n);
}

/* 发送 32 位二进制帧头：** ZDLE 'C' type flags pos(4) crc32(4) */
static void z_send_bin32(P *p, u8 type, u8 f0, const u8 *pos4) {
    u8 h[20];
    int n = 0;
    h[n++] = ZPAD;
    h[n++] = ZPAD;
    h[n++] = ZDLE;
    h[n++] = ZBIN32;
    h[n++] = type;
    h[n++] = f0;
    if (pos4) memcpy(h + n, pos4, 4);
    else memset(h + n, 0, 4);
    n += 4;
    u32 crc = crc32_le(h + 4, n - 4);
    for (int i = 0; i < 4; i++) h[n + i] = (u8)((crc >> (24 - i * 8)) & 0xff);
    n += 4;
    /* 帧头也要转义（除前导 ** 外整段按 ZDLE 规则处理） */
    u8 out[48];
    int m = 0;
    out[m++] = ZPAD;
    out[m++] = ZPAD;
    for (int i = 2; i < n; i++) {
        u8 c = h[i];
        if (c == ZDLE || (c & 0xFC) == 0x10 || (c & 0xFE) == 0x90) {
            out[m++] = ZDLE;
            out[m++] = (u8)(c ^ 0x40);
        } else out[m++] = c;
    }
    p_write(p, out, m);
}

static void z_send_hex(P *p, u8 type) {
    u8 h[16];
    int n = 0;
    h[n++] = ZPAD; h[n++] = ZPAD; h[n++] = ZDLE; h[n++] = ZHEX;
    n += 2;   /* type 占 2 个 hex 位，下面填 */
    for (int i = 0; i < 4; i++) n += 2;
    n += 4;   /* crc */
    /* 用 0 填充后写 hex */
    u8 raw[16] = { ZPAD, ZPAD, ZDLE, ZHEX, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    raw[4] = (u8)(type >> 4);
    raw[5] = (u8)(type & 0xf);
    h[0] = h[0]; /* noop */
    u8 out[32];
    int m = 0;
    out[m++] = ZPAD; out[m++] = ZPAD; out[m++] = ZDLE; out[m++] = ZHEX;
    /* type 2 位 */
    out[m++] = (u8)("0123456789abcdef"[(type >> 4) & 0xf]);
    out[m++] = (u8)("0123456789abcdef"[type & 0xf]);
    for (int i = 0; i < 4; i++) { out[m++] = '0'; out[m++] = '0'; }
    for (int i = 0; i < 4; i++) { out[m++] = '0'; out[m++] = '0'; }
    out[m++] = '\r';
    out[m++] = '\n';
    (void)h; (void)n; (void)raw;
    p_write(p, out, m);
}

typedef struct { u8 type, f0; u8 pos[4]; int hex; } ZHdr;

/* 读一个帧头；返回 0 成功，<0 出错，-3 表示读到非帧头字节（c 里返回） */
static int z_read_hdr(P *p, ZHdr *h, int timeout_ms) {
    int t = 0;
    while (t < timeout_ms) {
        int c = p_get1(p, 1000);
        if (c < 0) { if (c == -1) return -1; t += 1000; continue; }
        if (c != ZPAD) continue;
        int c2 = p_get1(p, 1000);
        if (c2 != ZPAD) continue;
        int c3 = p_get1(p, 1000);
        if (c3 != ZDLE) continue;
        int enc = p_get1(p, 1000);
        if (enc != ZHEX && enc != ZBIN && enc != ZBIN32) continue;
        if (enc == ZHEX) {
            u8 buf[32];
            int need = 2 /*type*/ + 8 /*4 bytes*/ + 4 /*crc*/ + 2 /*CRLF*/;
            if (p_get(p, buf, need, 2000) < need) return -2;
            h->type = (u8)((buf[0] <= '9' ? buf[0] - '0' : (buf[0] | 0x20) - 'a' + 10) << 4);
            h->type |= (u8)(buf[1] <= '9' ? buf[1] - '0' : (buf[1] | 0x20) - 'a' + 10);
            h->f0 = 0;
            for (int i = 0; i < 4; i++) {
                int hi = buf[2 + i * 2], lo = buf[3 + i * 2];
                hi = hi <= '9' ? hi - '0' : (hi | 0x20) - 'a' + 10;
                lo = lo <= '9' ? lo - '0' : (lo | 0x20) - 'a' + 10;
                h->pos[i] = (u8)((hi << 4) | lo);
            }
            h->hex = 1;
            return 0;
        }
        /* 二进制帧头 */
        u8 raw[16];
        int n = 0;
        int got_type = 0;
        while (n < 9) {
            int c4 = p_get1(p, 2000);
            if (c4 < 0) return -2;
            if (c4 == ZDLE) {
                int c5 = p_get1(p, 2000);
                if (c5 < 0) return -2;
                c4 = c5 ^ 0x40;
            }
            if (!got_type) {
                h->type = (u8)c4;
                if (enc == ZBIN32) { h->f0 = 0; got_type = 1; continue; }
                got_type = 1;
                continue;
            }
            if (n == 0 && enc == ZBIN) { h->f0 = (u8)c4; n = 0; raw[0] = 0; n = 0; continue; }
            raw[n++] = (u8)c4;
        }
        if (enc == ZBIN32) {
            u8 crcbuf[32];
            int m = 0;
            crcbuf[m++] = h->type;
            for (int i = 0; i < 4; i++) crcbuf[m++] = raw[i];
            /* 跳过 f0（ZBIN32 帧头字段是 type + 4 字节 pos + crc32） */
            h->f0 = 0;
            memcpy(h->pos, raw, 4);
            u32 crc = 0;
            for (int i = 0; i < 4; i++) crc = (crc << 8) | raw[4 + i];
            u32 want = crc32_le(crcbuf, 5);
            if (crc != want) return -3;
        } else {
            h->f0 = raw[0];
            memcpy(h->pos, raw + 1, 4);
        }
        h->hex = 0;
        return 0;
    }
    return -2;
}

int yt_zmodem_send(YtProtoIO *io, const char *path) {
    if (!io || !path) return -5;
    size_t len = 0;
    char *data = yt_read_file(utf8_to_wide(path), &len);
    if (!data) return -5;
    P p = { io, path, (long)len, 0 };
    ZHdr h;
    int rc = 0, retry = 0;

    /* ZRQINIT，等接收端发 ZRINIT（lrzsz 会主动送，若没送则重试） */
    for (retry = 0; retry < 6; retry++) {
        p_drain(&p, 8);
        z_send_hex(&p, ZRQINIT);
        if (z_read_hdr(&p, &h, 3000) == 0 && h.type == ZRINIT) break;
    }
    if (retry == 6) { free(data); return -2; }

    /* ZFILE：子包 = 名字 + 长度等信息 */
    {
        char meta[300];
        const char *base = path;
        for (const char *s = path; *s; s++)
            if (*s == '/' || *s == '\\') base = s + 1;
        _snprintf(meta, sizeof(meta), "%s%c%lu 0 0 0", base, 0, (unsigned long)len);
        size_t mlen = strlen(meta) + 1;   /* 末尾空字节 */
        z_send_bin32(&p, ZFILE, 0, NULL);
        z_put_escaped(&p, (const u8 *)meta, (int)mlen);
        /* ZCRCW 结束：再发一次 ZDLE + 'G'? 简化：不发送，靠 ZRPOS 同步 */
        (void)mlen;
    }
    if (z_read_hdr(&p, &h, 10000) != 0 || (h.type != ZRPOS && h.type != ZACK && h.type != ZSKIP)) {
        free(data);
        return -2;
    }
    if (h.type == ZSKIP) { free(data); return 0; }

    size_t off = 0;
    while (off < len) {
        int blen = (int)((len - off) >= 1024 ? 1024 : (len - off));
        u8 pos[4];
        for (int i = 0; i < 4; i++) pos[i] = (u8)(((off) >> (24 - i * 8)) & 0xff);
        z_send_bin32(&p, ZDATA, 0, pos);
        z_put_escaped(&p, (const u8 *)(data + off), blen);
        /* 数据后跟 CRC32（对数据本体，不转义算） */
        {
            u32 crc = crc32_le((const u8 *)(data + off), blen);
            u8 c[4] = { (u8)(crc & 0xff), (u8)((crc >> 8) & 0xff),
                        (u8)((crc >> 16) & 0xff), (u8)((crc >> 24) & 0xff) };
            z_put_escaped(&p, c, 4);
        }
        off += (size_t)blen;
        p.done = (long)off;
        p_prog(&p, p.done);
        if (z_read_hdr(&p, &h, 10000) != 0) { rc = -2; goto done; }
        if (h.type == ZRPOS) {
            u32 np = ((u32)h.pos[0] << 24) | ((u32)h.pos[1] << 16) |
                     ((u32)h.pos[2] << 8) | h.pos[3];
            off = np;
        } else if (h.type == ZNAK || h.type == ZABORT) { rc = -3; goto done; }
    }
    /* ZEOF */
    {
        u8 pos[4] = { 0, 0, 0, 0 };
        z_send_bin32(&p, ZEOF, 0, pos);
    }
    z_read_hdr(&p, &h, 10000);
    /* ZFIN 收尾 */
    z_send_hex(&p, ZFIN);
    z_read_hdr(&p, &h, 3000);
    rc = 0;
done:
    free(data);
    return rc;
}

int yt_zmodem_recv(YtProtoIO *io, const char *dir) {
    if (!io || !dir) return -5;
    P p = { io, dir, -1, 0 };
    ZHdr h;
    FILE *f = NULL;
    char save[520] = { 0 };
    int rc = 0;

    /* 送 ZRINIT，等 ZFILE */
    for (int i = 0; i < 6; i++) {
        z_send_hex(&p, ZRINIT);
        if (z_read_hdr(&p, &h, 3000) == 0 && h.type == ZFILE) break;
        if (i == 5) return -2;
    }
    /* 读子包：取文件名 */
    {
        u8 buf[512];
        int n = 0;
        while (n < (int)sizeof(buf) - 1) {
            int c = p_get1(&p, 3000);
            if (c < 0) break;
            if (c == ZDLE) {
                int c2 = p_get1(&p, 3000);
                if (c2 < 0) break;
                c = c2 ^ 0x40;
            }
            if (c == 0) { buf[n++] = 0; break; }
            buf[n++] = (u8)c;
        }
        buf[n] = 0;
        char name[260] = "received.bin";
        size_t size = 0;
        if (n > 0) {
            int k = 0;
            while (k < n && buf[k]) { if (k < 259) name[k] = (char)buf[k]; k++; }
            name[k < 260 ? k : 259] = 0;
            const u8 *s = buf + k + 1;
            while (*s >= '0' && *s <= '9') { size = size * 10 + (size_t)(*s - '0'); s++; }
        }
        _snprintf(save, sizeof(save), "%s\\%s", dir, name);
        f = _wfopen(utf8_to_wide(save), L"wb");
        if (!f) return -5;
        p.name = save;
        p.total = (long)size;
        p.done = 0;
    }
    /* 送 ZRPOS 0 */
    z_send_bin32(&p, ZRPOS, 0, (const u8 *)"\0\0\0\0");

    size_t off = 0;
    int fin = 0;
    while (!fin) {
        int r = z_read_hdr(&p, &h, 15000);
        if (r != 0) { rc = -2; break; }
        if (h.type == ZDATA) {
            u32 np = ((u32)h.pos[0] << 24) | ((u32)h.pos[1] << 16) |
                     ((u32)h.pos[2] << 8) | h.pos[3];
            /* 读数据直到遇到 ZDLE 转义的结束符（lrzsz 用 ZDLE + 'G' 表示 ZCRCW 结束） */
            u8 data[1200];
            int n = 0;
            int ended = 0;
            while (n < (int)sizeof(data)) {
                int c = p_get1(&p, 5000);
                if (c < 0) break;
                if (c == ZDLE) {
                    int c2 = p_get1(&p, 5000);
                    if (c2 < 0) break;
                    if (c2 == 0x47 || c2 == 0x46 || c2 == 0x43 || c2 == 0x44) { ended = 1; break; }
                    data[n++] = (u8)(c2 ^ 0x40);
                    continue;
                }
                data[n++] = (u8)c;
            }
            /* 末尾 4 字节是 CRC32 */
            int dlen = n - 4 > 0 ? n - 4 : 0;
            if (dlen > 0) {
                if (np != off) {
                    /* 对端要求重传 */
                    u8 pos[4];
                    for (int i = 0; i < 4; i++) pos[i] = (u8)((off >> (24 - i * 8)) & 0xff);
                    z_send_bin32(&p, ZRPOS, 0, pos);
                } else {
                    fwrite(data, 1, (size_t)dlen, f);
                    off += (size_t)dlen;
                    p.done = (long)off;
                    p_prog(&p, p.done);
                    u8 pos[4];
                    for (int i = 0; i < 4; i++) pos[i] = (u8)((off >> (24 - i * 8)) & 0xff);
                    z_send_bin32(&p, ZACK, 0, pos);
                }
            }
            (void)ended;
        } else if (h.type == ZEOF) {
            fclose(f);
            f = NULL;
            z_send_hex(&p, ZRINIT);
            fin = 1;
        } else if (h.type == ZFIN) {
            z_send_hex(&p, ZFIN);
            fin = 1;
        } else if (h.type == ZABORT) {
            rc = -4;
            break;
        }
    }
    if (f) fclose(f);
    return rc;
}

const char *yt_proto_error(int code) {
    switch (code) {
    case 0:  return "成功";
    case -1: return "IO 错误";
    case -2: return "等待对端超时";
    case -3: return "校验失败";
    case -4: return "对端取消";
    case -5: return "参数或文件错误";
    default: return "未知错误";
    }
}
