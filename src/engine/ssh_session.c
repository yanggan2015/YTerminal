#include "yt_core.h"
#include <libssh2.h>

typedef struct {
    SOCKET sock;
    SOCKET jump_sock;
    LIBSSH2_SESSION *session;
    LIBSSH2_SESSION *jump_session;
    LIBSSH2_CHANNEL *channel;
    HANDLE thr;
    volatile BOOL run;
} SshImpl;

static int waitsocket(SOCKET socket_fd, LIBSSH2_SESSION *session)
{
    struct timeval timeout;
    fd_set fd;
    fd_set *writefd = NULL;
    fd_set *readfd = NULL;
    int dir;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    FD_ZERO(&fd);
    FD_SET(socket_fd, &fd);
    dir = libssh2_session_block_directions(session);
    if (dir & LIBSSH2_SESSION_BLOCK_INBOUND) readfd = &fd;
    if (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND) writefd = &fd;
    return select((int)socket_fd + 1, readfd, writefd, NULL, &timeout);
}

static SOCKET tcp_connect_host(const wchar_t *host, int port, char *err, int errcap)
{
    char host8[YT_HOST_MAX], port8[16];
    struct addrinfo hints, *res = NULL, *p;
    SOCKET s = INVALID_SOCKET;
    WideCharToMultiByte(CP_UTF8, 0, host, -1, host8, sizeof(host8), NULL, NULL);
    snprintf(port8, sizeof(port8), "%d", port);
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host8, port8, &hints, &res) != 0) {
        snprintf(err, errcap, "resolve failed: %s", host8);
        return INVALID_SOCKET;
    }
    for (p = res; p; p = p->ai_next) {
        s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (connect(s, p->ai_addr, (int)p->ai_addrlen) == 0) break;
        closesocket(s);
        s = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (s == INVALID_SOCKET) snprintf(err, errcap, "connect failed: %s:%d", host8, port);
    return s;
}

static int auth_session(LIBSSH2_SESSION *session, YtSession *s, const wchar_t *user, const wchar_t *pass, const wchar_t *keyfile)
{
    char u8[64], p8[128], k8[YT_PATH_MAX];
    char *userauthlist;
    WideCharToMultiByte(CP_UTF8, 0, user, -1, u8, sizeof(u8), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, pass ? pass : L"", -1, p8, sizeof(p8), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, keyfile ? keyfile : L"", -1, k8, sizeof(k8), NULL, NULL);

    userauthlist = libssh2_userauth_list(session, u8, (unsigned)strlen(u8));
    if (k8[0] && userauthlist && strstr(userauthlist, "publickey")) {
        if (libssh2_userauth_publickey_fromfile(session, u8, NULL, k8, p8[0] ? p8 : NULL) == 0)
            return 0;
    }
    if (libssh2_userauth_password(session, u8, p8) == 0) return 0;
    snprintf(s->err, sizeof(s->err), "auth failed");
    return -1;
}

static DWORD WINAPI ssh_reader(LPVOID arg)
{
    YtSession *s = (YtSession *)arg;
    SshImpl *impl = (SshImpl *)s->impl;
    char buf[4096];
    while (impl->run) {
        ssize_t n;
        EnterCriticalSection(&s->lock);
        n = libssh2_channel_read(impl->channel, buf, sizeof(buf));
        LeaveCriticalSection(&s->lock);
        if (n > 0) {
            yt_session_post_out(s, buf, (int)n);
            /* drain without starving UI: brief yield after large burst */
            if (n >= (ssize_t)sizeof(buf) - 1) Sleep(0);
            continue;
        }
        if (n == LIBSSH2_ERROR_EAGAIN) {
            waitsocket(impl->sock, impl->session);
            continue;
        }
        break;
    }
    s->state = YT_ST_CLOSED;
    PostMessageW(s->hwnd_main, WM_YT_CLOSED, (WPARAM)s->tab_index, 0);
    return 0;
}

static void ssh_write(YtSession *s, const char *data, int len)
{
    /* 锁已由 yt_session_write 持有 */
    SshImpl *impl = (SshImpl *)s->impl;
    int left = len;
    if (!impl || !impl->channel) return;
    while (left > 0) {
        ssize_t n = libssh2_channel_write(impl->channel, data, left);
        if (n == LIBSSH2_ERROR_EAGAIN) {
            LeaveCriticalSection(&s->lock);
            waitsocket(impl->sock, impl->session);
            EnterCriticalSection(&s->lock);
            if (!impl->channel) break;
            continue;
        }
        if (n < 0) break;
        data += n; left -= (int)n;
    }
}

static void ssh_resize(YtSession *s, int cols, int rows)
{
    SshImpl *impl = (SshImpl *)s->impl;
    if (!impl || !impl->channel) return;
    /* 锁已由 yt_session_resize 持有 */
    libssh2_channel_request_pty_size(impl->channel, cols, rows);
}

static void ssh_close(YtSession *s)
{
    SshImpl *impl = (SshImpl *)s->impl;
    HANDLE thr;
    if (!impl) return;
    s->impl = NULL;
    s->write_fn = NULL;
    s->resize_fn = NULL;
    s->close_fn = NULL;
    impl->run = FALSE;
    if (impl->sock != INVALID_SOCKET) { closesocket(impl->sock); impl->sock = INVALID_SOCKET; }
    if (impl->jump_sock != INVALID_SOCKET) { closesocket(impl->jump_sock); impl->jump_sock = INVALID_SOCKET; }
    thr = impl->thr; impl->thr = NULL;
    if (thr) { WaitForSingleObject(thr, 800); CloseHandle(thr); }
    if (impl->channel) {
        libssh2_channel_close(impl->channel);
        libssh2_channel_free(impl->channel);
        impl->channel = NULL;
    }
    if (impl->session) {
        libssh2_session_disconnect(impl->session, "bye");
        libssh2_session_free(impl->session);
        impl->session = NULL;
    }
    if (impl->jump_session) {
        libssh2_session_disconnect(impl->jump_session, "bye");
        libssh2_session_free(impl->jump_session);
        impl->jump_session = NULL;
    }
    free(impl);
}

static LIBSSH2_SESSION *handshake(SOCKET sock, char *err, int errcap)
{
    LIBSSH2_SESSION *session = libssh2_session_init();
    int rc;
    if (!session) { snprintf(err, errcap, "session init failed"); return NULL; }
    libssh2_session_set_blocking(session, 1);
    rc = libssh2_session_handshake(session, sock);
    if (rc) {
        snprintf(err, errcap, "handshake failed %d", rc);
        libssh2_session_free(session);
        return NULL;
    }
    return session;
}

typedef struct {
    SOCKET s;
    LIBSSH2_CHANNEL *ch;
    LIBSSH2_SESSION *js;
    volatile BOOL run;
} JumpRelay;

static DWORD WINAPI jump_relay_thr(LPVOID arg)
{
    JumpRelay *r = (JumpRelay *)arg;
    char buf[4096];
    u_long nb = 1;
    ioctlsocket(r->s, FIONBIO, &nb);
    libssh2_session_set_blocking(r->js, 0);
    while (r->run) {
        fd_set fds; struct timeval tv = {0, 50000}; int n;
        FD_ZERO(&fds); FD_SET(r->s, &fds);
        if (select(0, &fds, NULL, NULL, &tv) > 0) {
            n = recv(r->s, buf, sizeof(buf), 0);
            if (n <= 0) break;
            {
                int left = n; char *p = buf;
                while (left > 0) {
                    ssize_t w = libssh2_channel_write(r->ch, p, left);
                    if (w == LIBSSH2_ERROR_EAGAIN) continue;
                    if (w < 0) { left = -1; break; }
                    p += w; left -= (int)w;
                }
                if (left < 0) break;
            }
        }
        n = (int)libssh2_channel_read(r->ch, buf, sizeof(buf));
        if (n > 0) send(r->s, buf, n, 0);
        else if (n != LIBSSH2_ERROR_EAGAIN) break;
    }
    closesocket(r->s);
    free(r);
    return 0;
}

BOOL yt_ssh_start(YtSession *s)
{
    SshImpl *impl = NULL;
    SOCKET sock = INVALID_SOCKET;
    LIBSSH2_SESSION *session = NULL;
    LIBSSH2_CHANNEL *channel = NULL;
    SOCKET jump_sock = INVALID_SOCKET;
    LIBSSH2_SESSION *jump_session = NULL;
    static LONG ssh_inited = 0;

    if (InterlockedCompareExchange(&ssh_inited, 1, 0) == 0) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        libssh2_init(0);
    }

    s->kind = YT_KIND_SSH;
    s->state = YT_ST_CONNECTING;
    if (!s->port) s->port = 22;

    if (s->jump_host[0]) {
        LIBSSH2_CHANNEL *jch;
        char th[YT_HOST_MAX];
        SOCKET listen_s, pair_a, pair_b;
        struct sockaddr_in addr;
        int alen = sizeof(addr);
        JumpRelay *jr;
        u_long nb;

        jump_sock = tcp_connect_host(s->jump_host, s->jump_port ? s->jump_port : 22, s->err, sizeof(s->err));
        if (jump_sock == INVALID_SOCKET) return FALSE;
        jump_session = handshake(jump_sock, s->err, sizeof(s->err));
        if (!jump_session) { closesocket(jump_sock); return FALSE; }
        if (auth_session(jump_session, s, s->jump_user[0] ? s->jump_user : s->user,
                         s->jump_pass[0] ? s->jump_pass : s->pass, s->keyfile) != 0) {
            libssh2_session_free(jump_session); closesocket(jump_sock); return FALSE;
        }
        WideCharToMultiByte(CP_UTF8, 0, s->host, -1, th, sizeof(th), NULL, NULL);
        jch = libssh2_channel_direct_tcpip(jump_session, th, s->port);
        if (!jch) {
            snprintf(s->err, sizeof(s->err), "ProxyJump failed");
            libssh2_session_free(jump_session); closesocket(jump_sock); return FALSE;
        }
        listen_s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ZeroMemory(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(listen_s, (struct sockaddr *)&addr, sizeof(addr));
        getsockname(listen_s, (struct sockaddr *)&addr, &alen);
        listen(listen_s, 1);
        pair_a = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        nb = 1; ioctlsocket(pair_a, FIONBIO, &nb);
        connect(pair_a, (struct sockaddr *)&addr, sizeof(addr));
        pair_b = accept(listen_s, NULL, NULL);
        nb = 0; ioctlsocket(pair_a, FIONBIO, &nb);
        closesocket(listen_s);
        jr = (JumpRelay *)calloc(1, sizeof(JumpRelay));
        jr->s = pair_b; jr->ch = jch; jr->js = jump_session; jr->run = TRUE;
        CreateThread(NULL, 0, jump_relay_thr, jr, 0, NULL);
        sock = pair_a;
        session = handshake(sock, s->err, sizeof(s->err));
        if (!session || auth_session(session, s, s->user, s->pass, s->keyfile) != 0) {
            jr->run = FALSE;
            if (session) libssh2_session_free(session);
            closesocket(sock);
            return FALSE;
        }
    } else {
        sock = tcp_connect_host(s->host, s->port, s->err, sizeof(s->err));
        if (sock == INVALID_SOCKET) return FALSE;
        session = handshake(sock, s->err, sizeof(s->err));
        if (!session) { closesocket(sock); return FALSE; }
        if (auth_session(session, s, s->user, s->pass, s->keyfile) != 0) {
            libssh2_session_free(session); closesocket(sock); return FALSE;
        }
    }

    channel = libssh2_channel_open_session(session);
    if (!channel) {
        snprintf(s->err, sizeof(s->err), "open session failed");
        libssh2_session_free(session); closesocket(sock); return FALSE;
    }
    if (libssh2_channel_request_pty(channel, "xterm-256color") ||
        libssh2_channel_request_pty_size(channel, YT_COLS, YT_ROWS) ||
        libssh2_channel_shell(channel)) {
        snprintf(s->err, sizeof(s->err), "pty/shell failed");
        libssh2_channel_free(channel); libssh2_session_free(session); closesocket(sock); return FALSE;
    }

    impl = (SshImpl *)calloc(1, sizeof(SshImpl));
    impl->sock = sock;
    impl->jump_sock = jump_sock;
    impl->jump_session = jump_session;
    impl->session = session;
    impl->channel = channel;
    impl->run = TRUE;
    s->impl = impl;
    s->write_fn = ssh_write;
    s->resize_fn = ssh_resize;
    s->close_fn = ssh_close;
    s->state = YT_ST_CONNECTED;
    if (!s->title[0])
        swprintf(s->title, YT_TITLE_MAX, L"%ls@%ls:%d", s->user, s->host, s->port);
    if (!s->name[0]) wcscpy(s->name, s->title);
    s->sftp = NULL;
    libssh2_session_set_blocking(session, 0);
    impl->thr = CreateThread(NULL, 0, ssh_reader, s, 0, NULL);
    if (s->forwards[0]) yt_fwd_start(s);
    return TRUE;
}

/* expose session pointer for sftp/forward */
LIBSSH2_SESSION *yt_ssh_libsession(YtSession *s)
{
    SshImpl *impl = s && s->impl ? (SshImpl *)s->impl : NULL;
    return impl ? impl->session : NULL;
}

SOCKET yt_ssh_sock(YtSession *s)
{
    SshImpl *impl = s && s->impl ? (SshImpl *)s->impl : NULL;
    return impl ? impl->sock : INVALID_SOCKET;
}
