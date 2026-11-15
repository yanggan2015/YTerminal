#include "yt_core.h"
#include <libssh2.h>

extern LIBSSH2_SESSION *yt_ssh_libsession(YtSession *s);

typedef struct {
    SOCKET listen_sock;
    HANDLE thr;
    volatile BOOL run;
    int local_port;
    char remote_host[256];
    int remote_port;
    YtSession *owner;
} FwdOne;

typedef struct {
    FwdOne *items;
    int n;
} FwdSet;

static struct { YtSession *s; FwdSet *set; } g_fwd_map[YT_MAX_TABS];

static FwdSet **map_slot(YtSession *s, BOOL create)
{
    int i, empty = -1;
    for (i = 0; i < YT_MAX_TABS; i++) {
        if (g_fwd_map[i].s == s) return &g_fwd_map[i].set;
        if (!g_fwd_map[i].s && empty < 0) empty = i;
    }
    if (create && empty >= 0) {
        g_fwd_map[empty].s = s;
        g_fwd_map[empty].set = NULL;
        return &g_fwd_map[empty].set;
    }
    return NULL;
}

static DWORD WINAPI fwd_accept_thr(LPVOID arg)
{
    FwdOne *f = (FwdOne *)arg;
    while (f->run) {
        SOCKET client;
        LIBSSH2_CHANNEL *ch;
        char buf[4096];
        fd_set rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(f->listen_sock, &rfds);
        tv.tv_sec = 0; tv.tv_usec = 200000;
        if (select(0, &rfds, NULL, NULL, &tv) <= 0) continue;
        client = accept(f->listen_sock, NULL, NULL);
        if (client == INVALID_SOCKET) continue;
        libssh2_session_set_blocking(yt_ssh_libsession(f->owner), 1);
        ch = libssh2_channel_direct_tcpip(yt_ssh_libsession(f->owner), f->remote_host, f->remote_port);
        if (!ch) { closesocket(client); libssh2_session_set_blocking(yt_ssh_libsession(f->owner), 0); continue; }
        {
            u_long nb = 1;
            ioctlsocket(client, FIONBIO, &nb);
            libssh2_session_set_blocking(yt_ssh_libsession(f->owner), 0);
            while (f->run) {
                int n;
                FD_ZERO(&rfds); FD_SET(client, &rfds);
                tv.tv_sec = 0; tv.tv_usec = 100000;
                if (select(0, &rfds, NULL, NULL, &tv) > 0) {
                    n = recv(client, buf, sizeof(buf), 0);
                    if (n <= 0) break;
                    {
                        int left = n; char *p = buf;
                        while (left > 0) {
                            ssize_t w = libssh2_channel_write(ch, p, left);
                            if (w == LIBSSH2_ERROR_EAGAIN) continue;
                            if (w < 0) { left = -1; break; }
                            p += w; left -= (int)w;
                        }
                        if (left < 0) break;
                    }
                }
                n = (int)libssh2_channel_read(ch, buf, sizeof(buf));
                if (n > 0) send(client, buf, n, 0);
                else if (n != LIBSSH2_ERROR_EAGAIN) break;
                if (libssh2_channel_eof(ch)) break;
            }
            libssh2_channel_close(ch);
            libssh2_channel_free(ch);
            closesocket(client);
        }
        PostMessageW(f->owner->hwnd_main, WM_YT_FWD_EVENT, (WPARAM)f->local_port, 0);
    }
    return 0;
}

BOOL yt_fwd_start(YtSession *s)
{
    char tmp[512], *tok, *ctx = NULL;
    FwdSet *set;
    FwdSet **slot;
    if (!s->forwards[0] || !yt_ssh_libsession(s)) return FALSE;
    /* defer actual listen setup; skip empty */
    if (!s->forwards[0]) return FALSE;
    slot = map_slot(s, TRUE);
    if (!slot) return FALSE;
    set = (FwdSet *)calloc(1, sizeof(FwdSet));
    strncpy(tmp, s->forwards, sizeof(tmp) - 1);
    tok = strtok_s(tmp, ",", &ctx);
    while (tok) {
        int lp = 0, rp = 0;
        char host[256] = {0};
        FwdOne *f;
        struct sockaddr_in addr;
        const char *p = tok;
        if (*p == 'L' || *p == 'l') p++;
        if (sscanf(p, "%d:%255[^:]:%d", &lp, host, &rp) != 3) {
            tok = strtok_s(NULL, ",", &ctx);
            continue;
        }
        set->items = (FwdOne *)realloc(set->items, sizeof(FwdOne) * (set->n + 1));
        f = &set->items[set->n];
        ZeroMemory(f, sizeof(*f));
        f->local_port = lp;
        f->remote_port = rp;
        strncpy(f->remote_host, host, sizeof(f->remote_host) - 1);
        f->owner = s;
        f->run = TRUE;
        f->listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ZeroMemory(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons((u_short)lp);
        if (bind(f->listen_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
            listen(f->listen_sock, 4) != 0) {
            closesocket(f->listen_sock);
            tok = strtok_s(NULL, ",", &ctx);
            continue;
        }
        f->thr = CreateThread(NULL, 0, fwd_accept_thr, f, 0, NULL);
        set->n++;
        tok = strtok_s(NULL, ",", &ctx);
    }
    *slot = set;
    return set->n > 0;
}

void yt_fwd_stop(YtSession *s)
{
    int i, j;
    FwdSet **slot = map_slot(s, FALSE);
    FwdSet *set;
    if (!slot || !*slot) return;
    set = *slot;
    for (i = 0; i < set->n; i++) {
        set->items[i].run = FALSE;
        closesocket(set->items[i].listen_sock);
        if (set->items[i].thr) {
            WaitForSingleObject(set->items[i].thr, 2000);
            CloseHandle(set->items[i].thr);
        }
    }
    free(set->items);
    free(set);
    for (j = 0; j < YT_MAX_TABS; j++) {
        if (g_fwd_map[j].s == s) { g_fwd_map[j].s = NULL; g_fwd_map[j].set = NULL; break; }
    }
}
