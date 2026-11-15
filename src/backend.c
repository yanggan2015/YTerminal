#define _WIN32_WINNT 0x0A00
#include "backend.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

typedef HRESULT(WINAPI *PFN_CreatePseudoConsole)(COORD, HANDLE, HANDLE, DWORD, HPCON *);
typedef HRESULT(WINAPI *PFN_ResizePseudoConsole)(HPCON, COORD);
typedef void (WINAPI *PFN_ClosePseudoConsole)(HPCON);

/* ------------------------------------------------------------------ */
/* 注册表                                                               */
/* ------------------------------------------------------------------ */
#define MAX_BACKENDS 512
static Backend *g_reg[MAX_BACKENDS];
static int g_next_id = 1;
static CRITICAL_SECTION g_lock;
static PFN_CreatePseudoConsole pCreatePseudoConsole;
static PFN_ResizePseudoConsole pResizePseudoConsole;
static PFN_ClosePseudoConsole pClosePseudoConsole;
static int g_conpty_ok;
static WSADATA g_wsadata;

struct Backend {
    int id;
    int kind;
    volatile LONG state;
    Session s;
    HWND notify;
    int cols, rows;
    HANDLE thread;
    volatile LONG stop;
    CRITICAL_SECTION wr_lock;
    char status[160];
    char error[512];
    void *impl;
};

void backend_init(void) {
    InitializeCriticalSection(&g_lock);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    pCreatePseudoConsole = (PFN_CreatePseudoConsole)GetProcAddress(k32, "CreatePseudoConsole");
    pResizePseudoConsole = (PFN_ResizePseudoConsole)GetProcAddress(k32, "ResizePseudoConsole");
    pClosePseudoConsole = (PFN_ClosePseudoConsole)GetProcAddress(k32, "ClosePseudoConsole");
    g_conpty_ok = pCreatePseudoConsole && pResizePseudoConsole && pClosePseudoConsole;
    WSAStartup(MAKEWORD(2, 2), &g_wsadata);
    SetEnvironmentVariableW(L"TERM", L"xterm-256color");
    SetEnvironmentVariableW(L"COLORTERM", L"truecolor");
}

void backend_shutdown(void) {
    WSACleanup();
}

Backend *backend_from_id(int id) {
    if (id <= 0) return NULL;
    EnterCriticalSection(&g_lock);
    Backend *r = NULL;
    for (int i = 0; i < MAX_BACKENDS; i++) {
        if (g_reg[i] && g_reg[i]->id == id) { r = g_reg[i]; break; }
    }
    LeaveCriticalSection(&g_lock);
    return r;
}

static Backend *backend_alloc(const Session *s, HWND notify, int cols, int rows, int kind) {
    Backend *b = (Backend *)xcalloc(1, sizeof(Backend));
    b->s = *s;
    b->kind = kind;
    b->notify = notify;
    b->cols = cols > 2 ? cols : 120;
    b->rows = rows > 1 ? rows : 30;
    b->state = BS_IDLE;
    InitializeCriticalSection(&b->wr_lock);
    _snprintf(b->status, sizeof(b->status), "就绪");
    EnterCriticalSection(&g_lock);
    b->id = g_next_id++;
    for (int i = 0; i < MAX_BACKENDS; i++) {
        if (!g_reg[i]) { g_reg[i] = b; break; }
    }
    LeaveCriticalSection(&g_lock);
    return b;
}

Backend *backend_create(const Session *s, HWND notify, int cols, int rows) {
    int kind = BK_LOCAL;
    if (s->protocol == PROTO_SSH) kind = BK_SSH;
    else if (s->protocol == PROTO_SERIAL) kind = BK_SERIAL;
    else if (s->protocol == PROTO_TELNET) kind = BK_TELNET;
    return backend_alloc(s, notify, cols, rows, kind);
}

int backend_id(Backend *b) { return b ? b->id : 0; }
int backend_kind(Backend *b) { return b ? b->kind : 0; }
int backend_state(Backend *b) { return b ? (int)b->state : 0; }
const char *backend_status(Backend *b) { return b ? b->status : ""; }
const char *backend_error(Backend *b) { return b ? b->error : ""; }
Session *backend_session(Backend *b) { return b ? &b->s : NULL; }
int backend_can_sftp(Backend *b) { return b && b->kind == BK_SSH; }
int backend_has_modem_lines(Backend *b) { return b && b->kind == BK_SERIAL; }
int backend_has_break(Backend *b) { return b && (b->kind == BK_SERIAL || b->kind == BK_TELNET); }

/* ------------------------------------------------------------------ */
/* 消息投递                                                             */
/* ------------------------------------------------------------------ */
static void post_state(Backend *b, int st) {
    InterlockedExchange(&b->state, st);
    if (b->notify) PostMessageW(b->notify, WM_YT_BACK_STATE, (WPARAM)b->id, (LPARAM)st);
}

static void post_data(Backend *b, const u8 *data, size_t len) {
    if (!len) return;
    BackData *m = (BackData *)xmalloc(sizeof(BackData) + len);
    m->id = b->id;
    m->len = len;
    memcpy(m->data, data, len);
    if (!b->notify || !PostMessageW(b->notify, WM_YT_BACK_DATA, (WPARAM)b->id, (LPARAM)m)) {
        free(m);
    }
}

static void post_error(Backend *b, const wchar_t *msg) {
    char *u = wide_to_utf8(msg);
    _snprintf(b->error, sizeof(b->error), "%s", u ? u : "未知错误");
    free(u);
    _snprintf(b->status, sizeof(b->status), "连接失败");
    post_state(b, BS_ERROR);
    if (b->notify) {
        char *copy = xstrdup(b->error);
        PostMessageW(b->notify, WM_YT_BACK_ERROR, (WPARAM)b->id, (LPARAM)copy);
    }
}

static void set_error_win32(Backend *b, DWORD err, const wchar_t *what) {
    wchar_t sys[512] = L"";
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, sys, 512, NULL);
    wchar_t msg[768];
    _snwprintf(msg, 768, L"%s 失败（错误 %lu）：%s", what, (unsigned long)err, sys);
    msg[767] = 0;
    post_error(b, msg);
}

/* ------------------------------------------------------------------ */
/* 工具                                                                 */
/* ------------------------------------------------------------------ */
static void find_ssh_exe(wchar_t *out, int cch) {
    wchar_t sysroot[MAX_PATH] = L"C:\\Windows";
    GetEnvironmentVariableW(L"SystemRoot", sysroot, MAX_PATH);
    _snwprintf(out, (size_t)cch, L"%s\\System32\\OpenSSH\\ssh.exe", sysroot);
    out[cch - 1] = 0;
    if (yt_file_exists(out)) return;
    wchar_t found[MAX_PATH];
    if (SearchPathW(NULL, L"ssh.exe", NULL, MAX_PATH, found, NULL)) {
        wcsncpy(out, found, (size_t)cch - 1);
        out[cch - 1] = 0;
    }
}

static void sb_append_arg(StrBuf *sb, const char *arg) {
    if (!arg) return;
    BOOL quote = strchr(arg, ' ') || strchr(arg, '\t') || strchr(arg, '"') || !*arg;
    if (!quote) { sb_append(sb, arg); return; }
    sb_append(sb, "\"");
    for (const char *p = arg; *p; p++) {
        if (*p == '"') sb_append(sb, "\\\"");
        else sb_append_n(sb, p, 1);
    }
    sb_append(sb, "\"");
}

static void append_tunnels(StrBuf *sb, const char *tunnels) {
    if (!tunnels || !*tunnels) return;
    char tmp[512];
    strncpy(tmp, tunnels, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    char *save = NULL;
    for (char *tok = strtok_s(tmp, ";,", &save); tok; tok = strtok_s(NULL, ";,", &save)) {
        while (*tok == ' ') tok++;
        char *end = tok + strlen(tok);
        while (end > tok && (end[-1] == ' ')) *--end = 0;
        if (!*tok) continue;
        int type = 0; /* 0=-L 1=-R 2=-D */
        char *spec = tok;
        if ((tok[0] == 'L' || tok[0] == 'l' || tok[0] == 'R' || tok[0] == 'r' || tok[0] == 'D' || tok[0] == 'd') &&
            (tok[1] == ':' || tok[1] == '=')) {
            if (tok[0] == 'R' || tok[0] == 'r') type = 1;
            else if (tok[0] == 'D' || tok[0] == 'd') type = 2;
            spec = tok + 2;
        }
        if (!*spec) continue;
        sb_append(sb, type == 0 ? " -L " : (type == 1 ? " -R " : " -D "));
        sb_append_arg(sb, spec);
    }
}

char *backend_build_ssh_command(const Session *s, int cols, int rows) {
    (void)cols; (void)rows;
    StrBuf sb; sb_init(&sb);
    wchar_t exe[MAX_PATH];
    find_ssh_exe(exe, MAX_PATH);
    char *exe_u = wide_to_utf8(exe);
    sb_append_arg(&sb, exe_u);
    free(exe_u);

    sb_appendf(&sb, " -tt -p %d", s->port > 0 ? s->port : 22);
    sb_append(&sb, " -o StrictHostKeyChecking=accept-new");
    sb_appendf(&sb, " -o ServerAliveInterval=%d", s->keepalive > 0 ? s->keepalive : 30);
    sb_append(&sb, " -o ServerAliveCountMax=3 -o ConnectTimeout=15");
    if (s->compression) sb_append(&sb, " -C");
    if (s->auth == AUTH_KEY && s->keyfile[0]) {
        sb_append(&sb, " -i ");
        sb_append_arg(&sb, s->keyfile);
        sb_append(&sb, " -o IdentitiesOnly=yes");
    }
    if (s->jump[0]) {
        sb_append(&sb, " -J ");
        sb_append_arg(&sb, s->jump);
    }
    append_tunnels(&sb, s->tunnels);
    if (s->extra_args[0]) {
        sb_append(&sb, " ");
        sb_append(&sb, s->extra_args);
    }
    sb_append(&sb, " ");
    if (s->user[0]) { sb_append_arg(&sb, s->user); sb_append(&sb, "@"); }
    sb_append_arg(&sb, s->host);
    return sb_detach(&sb);
}

/* ------------------------------------------------------------------ */
/* ConPTY（本地 / SSH）                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    HPCON hpc;
    HANDLE in_r, in_w, out_r, out_w;
    PROCESS_INFORMATION pi;
    int started;
} PtyImpl;

static DWORD WINAPI pty_reader(LPVOID param) {
    Backend *b = (Backend *)param;
    PtyImpl *p = (PtyImpl *)b->impl;
    u8 buf[8192];
    while (!InterlockedCompareExchange(&b->stop, 1, 1)) {
        DWORD got = 0;
        BOOL ok = ReadFile(p->out_r, buf, sizeof(buf), &got, NULL);
        if (!ok || got == 0) break;
        post_data(b, buf, got);
    }
    if (!b->stop) {
        _snprintf(b->status, sizeof(b->status), "已断开");
        post_state(b, BS_CLOSED);
    }
    return 0;
}

static BOOL start_conpty(Backend *b, const wchar_t *cmdline, const wchar_t *cwd) {
    if (!g_conpty_ok) {
        post_error(b, L"当前系统不支持 ConPTY（需要 Windows 10 1809 及以上）");
        return FALSE;
    }
    PtyImpl *p = (PtyImpl *)xcalloc(1, sizeof(PtyImpl));
    b->impl = p;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&p->in_r, &p->in_w, &sa, 0) ||
        !CreatePipe(&p->out_r, &p->out_w, &sa, 0)) {
        set_error_win32(b, GetLastError(), L"创建管道");
        return FALSE;
    }
    COORD size = { (SHORT)b->cols, (SHORT)b->rows };
    HRESULT hr = pCreatePseudoConsole(size, p->in_r, p->out_w, 0, &p->hpc);
    if (FAILED(hr)) {
        wchar_t msg[128];
        _snwprintf(msg, 128, L"CreatePseudoConsole 失败（0x%08lx）", (unsigned long)hr);
        post_error(b, msg);
        return FALSE;
    }

    STARTUPINFOEXW si;
    ZeroMemory(&si, sizeof(si));
    si.StartupInfo.cb = sizeof(si);
    SIZE_T attrs_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attrs_size);
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)xmalloc(attrs_size);
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrs_size)) {
        set_error_win32(b, GetLastError(), L"初始化进程属性");
        return FALSE;
    }
    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   p->hpc, sizeof(HPCON), NULL, NULL)) {
        set_error_win32(b, GetLastError(), L"绑定伪终端");
        return FALSE;
    }

    DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;
    wchar_t *cmd = xwcsdup(cmdline);
    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, flags, NULL, cwd, &si.StartupInfo, &p->pi);
    free(cmd);
    DeleteProcThreadAttributeList(si.lpAttributeList);
    free(si.lpAttributeList);
    if (!ok) {
        set_error_win32(b, GetLastError(), L"启动进程");
        return FALSE;
    }
    p->started = 1;
    b->thread = CreateThread(NULL, 0, pty_reader, b, 0, NULL);
    _snprintf(b->status, sizeof(b->status), "已连接");
    post_state(b, BS_CONNECTED);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* 串口                                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    HANDLE h;
} SerialImpl;

static int parity_const(int p) {
    switch (p) {
    case 1: return ODDPARITY;
    case 2: return EVENPARITY;
    case 3: return MARKPARITY;
    case 4: return SPACEPARITY;
    default: return NOPARITY;
    }
}
static int stopbits_const(int s) {
    if (s == 2) return TWOSTOPBITS;
    return ONESTOPBIT;
}

static DWORD WINAPI serial_reader(LPVOID param) {
    Backend *b = (Backend *)param;
    SerialImpl *p = (SerialImpl *)b->impl;
    u8 buf[4096];
    while (!InterlockedCompareExchange(&b->stop, 1, 1)) {
        DWORD got = 0;
        if (!ReadFile(p->h, buf, sizeof(buf), &got, NULL)) {
            DWORD err = GetLastError();
            if (err != ERROR_OPERATION_ABORTED) {
                _snprintf(b->status, sizeof(b->status), "串口读取失败");
                post_state(b, BS_CLOSED);
                break;
            }
            continue;
        }
        if (got) post_data(b, buf, got);
        else Sleep(2);
    }
    return 0;
}

static BOOL start_serial(Backend *b) {
    SerialImpl *p = (SerialImpl *)xcalloc(1, sizeof(SerialImpl));
    b->impl = p;
    wchar_t path[64];
    wchar_t *port = utf8_to_wide(b->s.port_name);
    _snwprintf(path, 64, L"\\\\.\\%s", port);
    path[63] = 0;
    free(port);
    p->h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (p->h == INVALID_HANDLE_VALUE) {
        set_error_win32(b, GetLastError(), L"打开串口");
        return FALSE;
    }
    SetupComm(p->h, 4096, 4096);
    DCB dcb;
    ZeroMemory(&dcb, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(p->h, &dcb)) {
        set_error_win32(b, GetLastError(), L"读取串口参数");
        return FALSE;
    }
    dcb.BaudRate = (DWORD)(b->s.baud > 0 ? b->s.baud : 115200);
    dcb.ByteSize = (BYTE)(b->s.databits >= 5 && b->s.databits <= 8 ? b->s.databits : 8);
    dcb.Parity = (BYTE)parity_const(b->s.parity);
    dcb.StopBits = (BYTE)stopbits_const(b->s.stopbits);
    dcb.fBinary = TRUE;
    dcb.fParity = (b->s.parity != 0);
    dcb.fOutxCtsFlow = (b->s.flow & 2) ? TRUE : FALSE;
    dcb.fRtsControl = (b->s.flow & 2) ? RTS_CONTROL_HANDSHAKE : (b->s.rts ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE);
    dcb.fOutX = (b->s.flow & 1) ? TRUE : FALSE;
    dcb.fInX = (b->s.flow & 1) ? TRUE : FALSE;
    dcb.fDtrControl = b->s.dtr ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
    dcb.fDsrSensitivity = (b->s.flow & 4) ? TRUE : FALSE;
    if (!SetCommState(p->h, &dcb)) {
        set_error_win32(b, GetLastError(), L"设置串口参数");
        return FALSE;
    }
    COMMTIMEOUTS ct;
    ct.ReadIntervalTimeout = 30;
    ct.ReadTotalTimeoutMultiplier = 0;
    ct.ReadTotalTimeoutConstant = 30;
    ct.WriteTotalTimeoutMultiplier = 0;
    ct.WriteTotalTimeoutConstant = 200;
    SetCommTimeouts(p->h, &ct);
    PurgeComm(p->h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    b->thread = CreateThread(NULL, 0, serial_reader, b, 0, NULL);
    char sub[96];
    session_subtitle(&b->s, sub, sizeof(sub));
    _snprintf(b->status, sizeof(b->status), "%s 已打开", sub);
    post_state(b, BS_CONNECTED);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Telnet                                                               */
/* ------------------------------------------------------------------ */
typedef struct {
    SOCKET sock;
    int parse;          /* 0 数据 1 IAC 2 命令 3 SB 4 SB-IAC */
    unsigned char verb, opt;
    int naws;
} TelnetImpl;

static void telnet_send_raw(TelnetImpl *t, const u8 *d, int n) {
    if (t->sock != INVALID_SOCKET) send(t->sock, (const char *)d, n, 0);
}

static void telnet_send_cmd(TelnetImpl *t, unsigned char verb, unsigned char opt) {
    u8 buf[3] = { 255, verb, opt };
    telnet_send_raw(t, buf, 3);
}

static void telnet_send_sub(TelnetImpl *t, unsigned char opt, const u8 *data, int n) {
    u8 head[2] = { 255, 250 };
    u8 tail[2] = { 255, 240 };
    telnet_send_raw(t, head, 2);
    u8 o = opt;
    telnet_send_raw(t, &o, 1);
    if (n) telnet_send_raw(t, data, n);
    telnet_send_raw(t, tail, 2);
}

static void telnet_will(TelnetImpl *t, unsigned char opt) {
    telnet_send_cmd(t, 251, opt);
    if (opt == 31) { /* NAWS */
        u8 d[4] = { 0, 80, 0, 24 };
        telnet_send_sub(t, 31, d, 4);
        t->naws = 1;
    } else if (opt == 24) { /* TTYPE */
        const char *tt = "xterm-256color";
        telnet_send_sub(t, 24, (const u8 *)tt, (int)strlen(tt));
    }
}

static void telnet_negotiate(TelnetImpl *t, unsigned char verb, unsigned char opt) {
    switch (verb) {
    case 251: /* WILL */
        if (opt == 1) telnet_send_cmd(t, 253, 1);         /* 服务器回显 -> DO ECHO */
        else if (opt == 3) telnet_send_cmd(t, 253, 3);    /* SGA */
        else if (opt == 24) telnet_send_cmd(t, 253, 24);  /* TTYPE */
        else if (opt == 31) { telnet_send_cmd(t, 253, 31); t->naws = 1; }
        else telnet_send_cmd(t, 254, opt);                /* DONT */
        break;
    case 252: /* WONT */
        break;
    case 253: /* DO */
        if (opt == 1) telnet_send_cmd(t, 252, 1);         /* 不回显 */
        else if (opt == 3) telnet_send_cmd(t, 251, 3);
        else if (opt == 24) telnet_will(t, 24);
        else if (opt == 31) telnet_will(t, 31);
        else telnet_send_cmd(t, 252, opt);                /* WONT */
        break;
    case 254: /* DONT */
        break;
    default: break;
    }
}

static DWORD WINAPI telnet_reader(LPVOID param) {
    Backend *b = (Backend *)param;
    TelnetImpl *t = (TelnetImpl *)b->impl;
    u8 in[4096];
    u8 out[4096];
    while (!InterlockedCompareExchange(&b->stop, 1, 1)) {
        int n = recv(t->sock, (char *)in, sizeof(in), 0);
        if (n <= 0) {
            _snprintf(b->status, sizeof(b->status), "对端关闭连接");
            post_state(b, BS_CLOSED);
            break;
        }
        int on = 0;
        for (int i = 0; i < n; i++) {
            u8 c = in[i];
            switch (t->parse) {
            case 0:
                if (c == 255) t->parse = 1;
                else if (c == 13) {
                    /* 忽略 CR，由 LF 处理 */
                } else out[on++] = c;
                break;
            case 1:
                if (c == 255) { out[on++] = 255; t->parse = 0; }
                else if (c == 250) t->parse = 3;
                else { t->verb = c; t->parse = 2; }
                break;
            case 2:
                telnet_negotiate(t, t->verb, c);
                t->parse = 0;
                break;
            case 3:
                if (c == 255) t->parse = 4;
                else if (c == 250) t->parse = 3;
                else if (c == 251 || c == 252 || c == 253 || c == 254) { t->verb = c; t->parse = 2; }
                else if (c == 240) t->parse = 0;
                break;
            case 4:
                if (c == 240) t->parse = 0;
                else t->parse = 3;
                break;
            default: t->parse = 0; break;
            }
        }
        if (on) post_data(b, out, (size_t)on);
    }
    return 0;
}

static BOOL start_telnet(Backend *b) {
    TelnetImpl *t = (TelnetImpl *)xcalloc(1, sizeof(TelnetImpl));
    b->impl = t;
    t->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (t->sock == INVALID_SOCKET) {
        post_error(b, L"创建套接字失败");
        return FALSE;
    }
    int port = b->s.port > 0 ? b->s.port : 23;
    struct sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    char host_u8[256];
    strncpy(host_u8, b->s.host, sizeof(host_u8) - 1);
    host_u8[sizeof(host_u8) - 1] = 0;
    struct addrinfo hints, *res = NULL;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host_u8, NULL, &hints, &res) != 0 || !res) {
        wchar_t msg[384];
        wchar_t *hw = utf8_to_wide(host_u8);
        _snwprintf(msg, 384, L"无法解析主机 %s", hw);
        msg[383] = 0;
        free(hw);
        post_error(b, msg);
        return FALSE;
    }
    addr.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
    freeaddrinfo(res);

    unsigned long nonblock = 1;
    ioctlsocket(t->sock, FIONBIO, &nonblock);
    connect(t->sock, (struct sockaddr *)&addr, sizeof(addr));
    fd_set wfds, efds;
    FD_ZERO(&wfds); FD_ZERO(&efds);
    FD_SET(t->sock, &wfds); FD_SET(t->sock, &efds);
    struct timeval tv = { 12, 0 };
    int sel = select(0, NULL, &wfds, &efds, &tv);
    if (sel <= 0) {
        wchar_t msg[384];
        _snwprintf(msg, 384, L"连接 %hs:%d 超时", host_u8, port);
        msg[383] = 0;
        post_error(b, msg);
        closesocket(t->sock);
        t->sock = INVALID_SOCKET;
        return FALSE;
    }
    int soerr = 0;
    int slen = sizeof(soerr);
    getsockopt(t->sock, SOL_SOCKET, SO_ERROR, (char *)&soerr, &slen);
    if (soerr != 0) {
        wchar_t msg[384];
        _snwprintf(msg, 384, L"连接 %hs:%d 失败（错误 %d）", host_u8, port, soerr);
        msg[383] = 0;
        post_error(b, msg);
        closesocket(t->sock);
        t->sock = INVALID_SOCKET;
        return FALSE;
    }
    nonblock = 0;
    ioctlsocket(t->sock, FIONBIO, &nonblock);
    int one = 1;
    setsockopt(t->sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
    b->thread = CreateThread(NULL, 0, telnet_reader, b, 0, NULL);
    _snprintf(b->status, sizeof(b->status), "已连接 %s:%d", host_u8, port);
    post_state(b, BS_CONNECTED);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* 生命周期                                                             */
/* ------------------------------------------------------------------ */
BOOL backend_start(Backend *b) {
    if (!b) return FALSE;
    if (b->state == BS_CONNECTED) return TRUE;
    post_state(b, BS_CONNECTING);
    _snprintf(b->status, sizeof(b->status), "连接中…");
    if (b->kind == BK_SERIAL) return start_serial(b);
    if (b->kind == BK_TELNET) return start_telnet(b);

    wchar_t *cmd = NULL;
    const wchar_t *cwd = b->s.cwd[0] ? b->s.cwd : NULL;
    if (b->kind == BK_LOCAL && !cwd) {
        const wchar_t *home = _wgetenv(L"USERPROFILE");
        if (home && home[0]) cwd = home;
    }
    if (b->kind == BK_SSH) {
        if (!b->s.host[0]) {
            post_error(b, L"未填写主机地址");
            return FALSE;
        }
        char *cmd_u8 = backend_build_ssh_command(&b->s, b->cols, b->rows);
        cmd = utf8_to_wide(cmd_u8);
        free(cmd_u8);
        cwd = NULL;
    } else {
        const char *shell = b->s.shell[0] ? b->s.shell : "powershell.exe -NoLogo -NoProfile";
        cmd = utf8_to_wide(shell);
    }
    BOOL ok = start_conpty(b, cmd, cwd);
    free(cmd);
    return ok;
}

BOOL backend_write(Backend *b, const void *data, size_t len) {
    if (!b || b->state != BS_CONNECTED || !data || !len) return FALSE;
    BOOL ok = FALSE;
    EnterCriticalSection(&b->wr_lock);
    if (b->kind == BK_SERIAL) {
        SerialImpl *p = (SerialImpl *)b->impl;
        DWORD wrote = 0;
        ok = WriteFile(p->h, data, (DWORD)len, &wrote, NULL) && wrote == (DWORD)len;
    } else if (b->kind == BK_TELNET) {
        TelnetImpl *t = (TelnetImpl *)b->impl;
        const char *s = (const char *)data;
        size_t sent = 0;
        if (len == 1 && s[0] == '\r') {
            const char *crlf = "\r\n";
            if (send(t->sock, crlf, 2, 0) == 2) { LeaveCriticalSection(&b->wr_lock); return TRUE; }
        }
        while (sent < len) {
            int n = send(t->sock, s + sent, (int)(len - sent), 0);
            if (n <= 0) break;
            sent += (size_t)n;
        }
        ok = sent == len;
    } else {
        PtyImpl *p = (PtyImpl *)b->impl;
        size_t sent = 0;
        const u8 *d = (const u8 *)data;
        while (sent < len) {
            DWORD wrote = 0;
            if (!WriteFile(p->in_w, d + sent, (DWORD)(len - sent), &wrote, NULL) || wrote == 0) break;
            sent += wrote;
        }
        ok = sent == len;
    }
    LeaveCriticalSection(&b->wr_lock);
    return ok;
}

void backend_resize(Backend *b, int cols, int rows) {
    if (!b) return;
    if (cols < 2) cols = 2;
    if (rows < 2) rows = 2;
    b->cols = cols; b->rows = rows;
    if (b->kind == BK_TELNET) {
        TelnetImpl *t = (TelnetImpl *)b->impl;
        if (t && t->naws) {
            u8 d[4] = { (u8)(cols >> 8), (u8)(cols & 0xFF), (u8)(rows >> 8), (u8)(rows & 0xFF) };
            telnet_send_sub(t, 31, d, 4);
        }
        return;
    }
    if ((b->kind == BK_LOCAL || b->kind == BK_SSH) && b->impl && g_conpty_ok) {
        PtyImpl *p = (PtyImpl *)b->impl;
        COORD size = { (SHORT)cols, (SHORT)rows };
        pResizePseudoConsole(p->hpc, size);
    }
}

void backend_send_break(Backend *b) {
    if (!b || b->state != BS_CONNECTED) return;
    if (b->kind == BK_SERIAL) {
        SerialImpl *p = (SerialImpl *)b->impl;
        SetCommBreak(p->h);
        Sleep(200);
        ClearCommBreak(p->h);
    } else if (b->kind == BK_TELNET) {
        TelnetImpl *t = (TelnetImpl *)b->impl;
        telnet_send_cmd(t, 243, 0); /* IAC BREAK */
    }
}

void backend_set_line(Backend *b, int dtr, int rts) {
    if (!b || b->kind != BK_SERIAL || !b->impl) return;
    SerialImpl *p = (SerialImpl *)b->impl;
    EscapeCommFunction(p->h, dtr ? SETDTR : CLRDTR);
    EscapeCommFunction(p->h, rts ? SETRTS : CLRRTS);
}

void backend_close(Backend *b) {
    if (!b) return;
    InterlockedExchange(&b->stop, 1);
    if (b->kind == BK_SERIAL) {
        SerialImpl *p = (SerialImpl *)b->impl;
        if (p && p->h != INVALID_HANDLE_VALUE) {
            PurgeComm(p->h, PURGE_RXCLEAR | PURGE_TXCLEAR);
            CancelIoEx(p->h, NULL);
            CloseHandle(p->h);
        }
    } else if (b->kind == BK_TELNET) {
        TelnetImpl *t = (TelnetImpl *)b->impl;
        if (t && t->sock != INVALID_SOCKET) {
            shutdown(t->sock, SD_BOTH);
            closesocket(t->sock);
        }
    } else {
        PtyImpl *p = (PtyImpl *)b->impl;
        if (p) {
            if (p->hpc) pClosePseudoConsole(p->hpc);
            if (p->pi.hProcess) {
                if (p->started) {
                    TerminateProcess(p->pi.hProcess, 0);
                }
                CloseHandle(p->pi.hProcess);
                CloseHandle(p->pi.hThread);
            }
            if (p->in_r) CloseHandle(p->in_r);
            if (p->in_w) CloseHandle(p->in_w);
            if (p->out_r) CloseHandle(p->out_r);
            if (p->out_w) CloseHandle(p->out_w);
        }
    }
    if (b->thread) {
        WaitForSingleObject(b->thread, 2000);
        CloseHandle(b->thread);
        b->thread = NULL;
    }
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < MAX_BACKENDS; i++) {
        if (g_reg[i] == b) { g_reg[i] = NULL; break; }
    }
    LeaveCriticalSection(&g_lock);
    free(b->impl);
    b->impl = NULL;
    DeleteCriticalSection(&b->wr_lock);
    free(b);
}


