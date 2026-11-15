/* tools/yproto.c : 鍗忚寮曟搸鍛戒护琛屽す鍏凤紙绾?C锛屼覆鍙ｇ洿杩烇級
 *
 * 鐢ㄦ硶锛? *   yproto send <x|y|z> <COMx:baud> <鏈湴鏂囦欢> ["杩滅鍛戒护"]
 *   yproto recv <x|y|z> <COMx:baud> <淇濆瓨鐩綍> ["杩滅鍛戒护"]
 * 渚嬶細
 *   yproto recv z COM3:1500000 build\dl "sz /tmp/probe.bin"
 *   yproto send z COM3:1500000 build\probe.bin "cd /tmp && rz -y"
 */
#include "proto.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

typedef struct {
    HANDLE h;
    long long t0;
} Ser;

static long long now_ms(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (long long)(c.QuadPart * 1000 / f.QuadPart);
}

static int ser_put(void *ctx, const u8 *b, int len) {
    Ser *s = (Ser *)ctx;
    int done = 0;
    while (done < len) {
        DWORD w = 0;
        if (!WriteFile(s->h, b + done, (DWORD)(len - done), &w, NULL)) return -1;
        if (!w) return -1;
        done += (int)w;
    }
    return done;
}

static int ser_get(void *ctx, u8 *b, int len, int timeout_ms) {
    Ser *s = (Ser *)ctx;
    long long end = now_ms() + timeout_ms;
    int got = 0;
    while (got < len) {
        DWORD n = 0;
        if (!ReadFile(s->h, b + got, (DWORD)(len - got), &n, NULL)) return -1;
        if (n > 0) { got += (int)n; continue; }
        if (now_ms() >= end) break;
        Sleep(1);
    }
    return got;
}

static void ser_prog(void *ctx, const char *name, long done, long total) {
    (void)ctx;
    if (total > 0) printf("\r  %s  %ld/%ld (%ld%%)", name, done, total, done * 100 / total);
    else printf("\r  %s  %ld", name, done);
    fflush(stdout);
}

static HANDLE open_serial(const char *spec, int *baud_out) {
    char port[32] = "COM3";
    int baud = 115200;
    const char *colon = strchr(spec, ':');
    if (colon) {
        size_t n = (size_t)(colon - spec);
        if (n > sizeof(port) - 1) n = sizeof(port) - 1;
        memcpy(port, spec, n);
        port[n] = 0;
        baud = atoi(colon + 1);
    } else {
        _snprintf(port, sizeof(port), "%s", spec);
    }
    if (baud_out) *baud_out = baud;
    char path[64];
    _snprintf(path, sizeof(path), "\\\\.\\%s", port);
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return h;
    SetupComm(h, 1 << 18, 1 << 18);
    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) { CloseHandle(h); return INVALID_HANDLE_VALUE; }
    dcb.BaudRate = (DWORD)baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    if (!SetCommState(h, &dcb)) { CloseHandle(h); return INVALID_HANDLE_VALUE; }
    COMMTIMEOUTS to;
    to.ReadIntervalTimeout = MAXDWORD;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant = 20;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = 500;
    SetCommTimeouts(h, &to);
    return h;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        printf("usage: yproto send|recv x|y|z <COMx:baud> <file|dir> [remote-cmd]\n");
        return 2;
    }
    const char *mode = argv[1], *kind = argv[2], *port = argv[3], *path = argv[4];
    const char *cmd = argc > 5 ? argv[5] : NULL;
    int baud = 0;
    HANDLE h = open_serial(port, &baud);
    if (h == INVALID_HANDLE_VALUE) {
        printf("open %s failed: %lu\n", port, (unsigned long)GetLastError());
        return 3;
    }
    Ser s = { h, 0 };
    YtProtoIO io;
    memset(&io, 0, sizeof(io));
    io.ctx = &s;
    io.put = ser_put;
    io.get = ser_get;
    io.progress = ser_prog;
    printf("port=%s baud=%d mode=%s proto=%s %s\n", port, baud, mode, kind, path);

    if (cmd) {
        u8 cr = 13;
        u8 c3 = 3;
        for (int i = 0; i < 6; i++) { ser_put(&s, &c3, 1); Sleep(20); }
        ser_put(&s, &cr, 1);
        Sleep(300);
        ser_put(&s, (const u8 *)cmd, (int)strlen(cmd));
        ser_put(&s, &cr, 1);
        printf("sent remote cmd: %s\n", cmd);
        Sleep(600);
    }

    if (strcmp(mode, "xprobe") == 0) {
        /* 反复送 'C' 直到对端开始发块，然后把第一块（131 字节）原样落盘 */
        long long end = now_ms() + 8000;
        u8 blk[200];
        int got = 0;
        while (now_ms() < end && !got) {
            u8 c = 'C';
            ser_put(&s, &c, 1);
            long long slice = now_ms() + 200;
            while (now_ms() < slice) {
                u8 b[64];
                int n = ser_get(&s, b, 64, 40);
                for (int i = 0; i < n; i++) {
                    if (blk[0] == 0 && b[i] != 0x01 && b[i] != 0x02) continue;
                    if (got < (int)sizeof(blk)) blk[got++] = b[i];
                }
                if (got >= 3 + 128) { got = -got; break; }
            }
            if (got < 0) { got = -got; break; }
        }
        FILE *f = fopen("build\\blk.bin", "wb");
        if (f) { fwrite(blk, 1, (size_t)(got > 0 ? got : 0), f); fclose(f); }
        printf("[xprobe] captured %d bytes -> build\\blk.bin\n", got > 0 ? got : 0);
        CloseHandle(h);
        return 0;
    }

    if (strcmp(mode, "dump") == 0) {
        long long end = now_ms() + 5000;
        while (now_ms() < end) {
            u8 b[256];
            int n = ser_get(&s, b, 256, 50);
            for (int i = 0; i < n; i++) printf("%02x ", b[i]);
            if (n) printf("\n");
        }
        printf("\\n[dump done]\\n");
        CloseHandle(h);
        return 0;
    }

    int rc;
    if (strcmp(mode, "send") == 0) {
        if (!strcmp(kind, "x")) rc = yt_xmodem_send(&io, path, 1);
        else if (!strcmp(kind, "y")) rc = yt_ymodem_send(&io, path);
        else rc = yt_zmodem_send(&io, path);
    } else {
        if (!strcmp(kind, "x")) { char out[600]; _snprintf(out, sizeof(out), "%s\\xmodem_recv.bin", path); rc = yt_xmodem_recv(&io, out, 1); }
        else if (!strcmp(kind, "y")) rc = yt_ymodem_recv(&io, path);
        else rc = yt_zmodem_recv(&io, path);
    }
    printf("\nresult=%d (%s)\n", rc, yt_proto_error(rc));
    CloseHandle(h);
    return rc == 0 ? 0 : 1;
}
