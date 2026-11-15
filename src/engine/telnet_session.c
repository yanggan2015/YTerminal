#include "yt_core.h"

/* 独立 Telnet 模块：原始 TCP + 最小 IAC 协商，不污染 SSH/串口代码 */

typedef struct {
    SOCKET sock;
    HANDLE thr;
    volatile LONG run;
} TelImpl;

static void tel_iac_reply(SOCKET sock, unsigned char cmd, unsigned char opt)
{
    unsigned char buf[3] = { 255 /*IAC*/, cmd, opt };
    send(sock, (char *)buf, 3, 0);
}

/* 过滤 IAC：对 DO/DONT/WILL/WONT 一律拒绝，避免协商卡死 */
static int tel_filter(TelImpl *p, const unsigned char *in, int n, char *out, int ocap)
{
    int i = 0, o = 0;
    while (i < n && o < ocap) {
        if (in[i] != 255) { out[o++] = (char)in[i++]; continue; }
        if (i + 1 >= n) break;
        if (in[i + 1] == 255) { out[o++] = (char)255; i += 2; continue; } /* escaped */
        if (in[i + 1] == 251 /*WILL*/ || in[i + 1] == 252 /*WONT*/) {
            if (i + 2 < n) { tel_iac_reply(p->sock, 254 /*DONT*/, in[i + 2]); i += 3; }
            else break;
            continue;
        }
        if (in[i + 1] == 253 /*DO*/ || in[i + 1] == 254 /*DONT*/) {
            if (i + 2 < n) { tel_iac_reply(p->sock, 252 /*WONT*/, in[i + 2]); i += 3; }
            else break;
            continue;
        }
        i += 2; /* 其它命令跳过 */
    }
    return o;
}

static DWORD WINAPI tel_reader(LPVOID arg)
{
    YtSession *s = (YtSession *)arg;
    TelImpl *p = (TelImpl *)s->impl;
    unsigned char raw[4096];
    char out[4096];
    while (p && InterlockedCompareExchange(&p->run, 1, 1) == 1) {
        int n = recv(p->sock, (char *)raw, sizeof(raw), 0);
        if (n == 0) break;
        if (n < 0) {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK) { Sleep(10); continue; }
            break;
        }
        {
            int m = tel_filter(p, raw, n, out, sizeof(out));
            if (m > 0 && s->hwnd_term) yt_session_post_out(s, out, m);
        }
    }
    if (s->hwnd_main)
        PostMessageW(s->hwnd_main, WM_YT_CLOSED, (WPARAM)s->tab_index, 0);
    return 0;
}

static void tel_write(YtSession *s, const char *data, int len)
{
    TelImpl *p = (TelImpl *)s->impl;
    int off = 0;
    if (!p || p->sock == INVALID_SOCKET || len <= 0) return;
    while (off < len) {
        int n = send(p->sock, data + off, len - off, 0);
        if (n <= 0) break;
        off += n;
    }
}

static void tel_close(YtSession *s)
{
    TelImpl *p = (TelImpl *)s->impl;
    HANDLE thr;
    if (!p) return;
    s->impl = NULL;
    s->write_fn = NULL;
    s->close_fn = NULL;
    InterlockedExchange(&p->run, 0);
    if (p->sock != INVALID_SOCKET) {
        shutdown(p->sock, SD_BOTH);
        closesocket(p->sock);
        p->sock = INVALID_SOCKET;
    }
    thr = p->thr; p->thr = NULL;
    if (thr) {
        if (WaitForSingleObject(thr, 5000) != WAIT_OBJECT_0)
            TerminateThread(thr, 0);
        CloseHandle(thr);
    }
    free(p);
}

BOOL yt_telnet_start(YtSession *s)
{
    SOCKET sock;
    struct addrinfoW hints, *res = NULL, *rp;
    wchar_t portstr[16];
    TelImpl *p;
    u_long nb = 1;
    int port = s->port > 0 ? s->port : 23;
    static LONG wsa_once;
    WSADATA wsa;

    if (!s->host[0]) {
        strncpy(s->err, "telnet host empty", sizeof(s->err) - 1);
        return FALSE;
    }
    if (InterlockedCompareExchange(&wsa_once, 1, 0) == 0)
        WSAStartup(MAKEWORD(2, 2), &wsa);
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    swprintf(portstr, 16, L"%d", port);
    if (GetAddrInfoW(s->host, portstr, &hints, &res) != 0) {
        snprintf(s->err, sizeof(s->err), "telnet resolve failed");
        return FALSE;
    }
    sock = INVALID_SOCKET;
    for (rp = res; rp; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == INVALID_SOCKET) continue;
        if (connect(sock, rp->ai_addr, (int)rp->ai_addrlen) == 0) break;
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    FreeAddrInfoW(res);
    if (sock == INVALID_SOCKET) {
        snprintf(s->err, sizeof(s->err), "telnet connect failed");
        return FALSE;
    }
    ioctlsocket(sock, FIONBIO, &nb);

    p = (TelImpl *)calloc(1, sizeof(TelImpl));
    p->sock = sock;
    InterlockedExchange(&p->run, 1);
    s->impl = p;
    s->kind = YT_KIND_TELNET;
    s->write_fn = tel_write;
    s->close_fn = tel_close;
    s->state = YT_ST_CONNECTED;
    swprintf(s->title, YT_TITLE_MAX, L"Telnet:%ls:%d", s->host, port);
    if (!s->name[0]) wcscpy(s->name, s->title);
    p->thr = CreateThread(NULL, 0, tel_reader, s, 0, NULL);
    return TRUE;
}
