#include "yt_core.h"

typedef struct {
    HANDLE com;
    HANDLE reader;
    HANDLE writer;
    HANDLE wr_event;
    CRITICAL_SECTION wlock;
    char *wbuf;
    int wlen, wcap;
    volatile LONG run;
    YtSession *owner;
} SerImpl;

static DWORD WINAPI ser_reader(LPVOID arg)
{
    SerImpl *p = (SerImpl *)arg;
    char buf[16384];
    char pending[65536];
    int pend = 0;
    DWORD rd;
    DWORD last_flush = GetTickCount();

    while (InterlockedCompareExchange(&p->run, 1, 1) == 1) {
        HANDLE com = p->com;
        if (!com) break;
        if (!ReadFile(com, buf, sizeof(buf), &rd, NULL)) {
            DWORD e = GetLastError();
            if (e == ERROR_OPERATION_ABORTED || e == ERROR_INVALID_HANDLE) break;
            Sleep(5);
            continue;
        }
        if (rd > 0) {
            if (pend + (int)rd > (int)sizeof(pending)) {
                YtSession *s = p->owner;
                if (pend > 0 && s && s->hwnd_term) yt_session_post_out(s, pending, pend);
                pend = 0;
                s = p->owner;
                if (s && s->hwnd_term) yt_session_post_out(s, buf, (int)rd);
                last_flush = GetTickCount();
            } else {
                memcpy(pending + pend, buf, rd);
                pend += (int)rd;
            }
        }
        if (pend > 0 && (rd == 0 || pend >= 4096 || (GetTickCount() - last_flush) >= 16)) {
            YtSession *s = p->owner;
            if (s && s->hwnd_term) yt_session_post_out(s, pending, pend);
            pend = 0;
            last_flush = GetTickCount();
        }
        if (rd == 0) Sleep(1);
    }
    {
        YtSession *s = p->owner;
        if (pend > 0 && s && s->hwnd_term) yt_session_post_out(s, pending, pend);
        if (s && s->hwnd_main)
            PostMessageW(s->hwnd_main, WM_YT_CLOSED, (WPARAM)s->tab_index, 0);
    }
    return 0;
}

static DWORD WINAPI ser_writer(LPVOID arg)
{
    SerImpl *p = (SerImpl *)arg;
    while (InterlockedCompareExchange(&p->run, 1, 1) == 1) {
        char local[8192];
        int n = 0, off;
        DWORD wr;
        WaitForSingleObject(p->wr_event, 100);
        if (InterlockedCompareExchange(&p->run, 1, 1) != 1) break;
        EnterCriticalSection(&p->wlock);
        if (p->wlen > 0) {
            n = p->wlen < (int)sizeof(local) ? p->wlen : (int)sizeof(local);
            memcpy(local, p->wbuf, n);
            if (n < p->wlen) memmove(p->wbuf, p->wbuf + n, p->wlen - n);
            p->wlen -= n;
        }
        LeaveCriticalSection(&p->wlock);
        if (n <= 0 || !p->com) continue;
        off = 0;
        while (off < n && InterlockedCompareExchange(&p->run, 1, 1) == 1) {
            if (!WriteFile(p->com, local + off, (DWORD)(n - off), &wr, NULL) || wr == 0) break;
            off += (int)wr;
        }
    }
    return 0;
}

static void ser_write(YtSession *s, const char *data, int len)
{
    SerImpl *p = (SerImpl *)s->impl;
    if (!p || !data || len <= 0) return;
    EnterCriticalSection(&p->wlock);
    if (p->wlen + len > p->wcap) {
        int nc = p->wcap ? p->wcap * 2 : 8192;
        while (nc < p->wlen + len) nc *= 2;
        p->wbuf = (char *)realloc(p->wbuf, nc);
        p->wcap = nc;
    }
    if (p->wbuf) {
        memcpy(p->wbuf + p->wlen, data, len);
        p->wlen += len;
    }
    LeaveCriticalSection(&p->wlock);
    SetEvent(p->wr_event);
}

static void ser_close(YtSession *s)
{
    SerImpl *p = (SerImpl *)s->impl;
    HANDLE com, reader, writer;
    if (!p) return;
    s->impl = NULL;
    s->write_fn = NULL;
    s->close_fn = NULL;
    p->owner = NULL;
    InterlockedExchange(&p->run, 0);
    if (p->wr_event) SetEvent(p->wr_event);

    com = p->com; p->com = NULL;
    if (com) {
        PurgeComm(com, PURGE_RXABORT | PURGE_TXABORT | PURGE_RXCLEAR | PURGE_TXCLEAR);
        CancelIoEx(com, NULL);
        CloseHandle(com);
    }

    reader = p->reader; p->reader = NULL;
    writer = p->writer; p->writer = NULL;
    if (reader) { WaitForSingleObject(reader, 800); CloseHandle(reader); }
    if (writer) { WaitForSingleObject(writer, 800); CloseHandle(writer); }
    if (p->wr_event) { CloseHandle(p->wr_event); p->wr_event = NULL; }
    DeleteCriticalSection(&p->wlock);
    free(p->wbuf);
    free(p);
}

BOOL yt_serial_start(YtSession *s)
{
    wchar_t path[64];
    DCB dcb;
    COMMTIMEOUTS to;
    SerImpl *p;
    HANDLE h;
    DWORD baud;

    if (!s->com[0]) {
        strncpy(s->err, "COM empty", sizeof(s->err) - 1);
        return FALSE;
    }
    swprintf(path, 64, L"\\\\.\\%ls", s->com);
    h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        snprintf(s->err, sizeof(s->err), "open %ls err %lu", s->com, GetLastError());
        return FALSE;
    }

    baud = s->baud > 0 ? (DWORD)s->baud : 115200;
    SetupComm(h, 256 * 1024, 64 * 1024);

    ZeroMemory(&dcb, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        snprintf(s->err, sizeof(s->err), "GetCommState %lu", GetLastError());
        CloseHandle(h); return FALSE;
    }
    dcb.BaudRate = baud;
    dcb.ByteSize = (BYTE)(s->data_bits ? s->data_bits : 8);
    dcb.Parity = s->parity == 1 ? ODDPARITY : s->parity == 2 ? EVENPARITY : NOPARITY;
    dcb.StopBits = s->stop_bits == 2 ? TWOSTOPBITS : ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = (dcb.Parity != NOPARITY);
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = TRUE;
    if (s->flow == 1) {
        dcb.fOutxCtsFlow = TRUE;
        dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fOutX = dcb.fInX = FALSE;
    } else if (s->flow == 2) {
        dcb.fOutX = dcb.fInX = TRUE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
    } else {
        dcb.fOutX = dcb.fInX = FALSE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
    }
    if (!SetCommState(h, &dcb)) {
        snprintf(s->err, sizeof(s->err), "SetCommState %lu", GetLastError());
        CloseHandle(h); return FALSE;
    }

    ZeroMemory(&to, sizeof(to));
    to.ReadIntervalTimeout = 1;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant = 20;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = 500;
    SetCommTimeouts(h, &to);
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    p = (SerImpl *)calloc(1, sizeof(SerImpl));
    p->com = h;
    p->owner = s;
    InterlockedExchange(&p->run, 1);
    p->wr_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    InitializeCriticalSection(&p->wlock);
    s->impl = p;
    s->kind = YT_KIND_SERIAL;
    s->write_fn = ser_write;
    s->close_fn = ser_close;
    s->state = YT_ST_CONNECTED;
    swprintf(s->title, YT_TITLE_MAX, L"%ls @ %lu", s->com, (unsigned long)baud);
    if (!s->name[0]) wcscpy(s->name, s->title);
    p->reader = CreateThread(NULL, 0, ser_reader, p, 0, NULL);
    p->writer = CreateThread(NULL, 0, ser_writer, p, 0, NULL);
    return TRUE;
}
