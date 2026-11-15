#include "yt_core.h"

void yt_session_init(YtSession *s)
{
    ZeroMemory(s, sizeof(*s));
    InitializeCriticalSection(&s->lock);
    s->port = 22;
    s->baud = 115200;
    s->data_bits = 8;
    s->stop_bits = 1;
    s->jump_port = 22;
    s->split_with = -1;
    s->tab_index = -1;
    wcscpy(s->shell, L"powershell");
    wcscpy(s->com, L"COM3");
    s->state = YT_ST_IDLE;
}

void yt_session_post_out(YtSession *s, const char *buf, int n)
{
    char *p;
    if (!s || !s->hwnd_term || n <= 0) return;
    p = (char *)malloc((size_t)n + 1);
    if (!p) return;
    memcpy(p, buf, (size_t)n);
    p[n] = 0;
    if (s->log_fp) {
        /* raw stream; avoid per-chunk timestamp + fflush (major lag) */
        fwrite(buf, 1, (size_t)n, s->log_fp);
    }
    if (!PostMessageW(s->hwnd_term, WM_YT_OUTPUT, (WPARAM)n, (LPARAM)p))
        free(p);
}

void yt_session_write(YtSession *s, const char *data, int len)
{
    if (!s || !s->write_fn || s->state != YT_ST_CONNECTED) return;
    EnterCriticalSection(&s->lock);
    s->write_fn(s, data, len);
    LeaveCriticalSection(&s->lock);
}

void yt_session_resize(YtSession *s, int cols, int rows)
{
    if (!s || !s->resize_fn) return;
    EnterCriticalSection(&s->lock);
    s->resize_fn(s, cols, rows);
    LeaveCriticalSection(&s->lock);
}

void yt_session_close(YtSession *s)
{
    YtCloseFn cf = NULL;
    if (!s) return;
    {
        void *sftp;
        EnterCriticalSection(&s->lock);
        s->state = YT_ST_CLOSED;
        s->hwnd_term = NULL;
        sftp = s->sftp; s->sftp = NULL;
        yt_fwd_stop(s);
        cf = s->close_fn;
        s->close_fn = NULL;
        s->write_fn = NULL;
        s->resize_fn = NULL;
        LeaveCriticalSection(&s->lock);
        if (sftp) yt_sftp_close(sftp); /* 在锁外关，避免与 sftp 内再加锁缠死 */
    }
    if (cf) cf(s);
}

void yt_session_free(YtSession *s)
{
    yt_session_close(s);
    if (s->log_fp) { fclose(s->log_fp); s->log_fp = NULL; }
    DeleteCriticalSection(&s->lock);
}

void yt_sync_broadcast(YtSession **tabs, int n, int from, const char *data, int len)
{
    int i;
    if (!tabs || from < 0 || from >= n || !tabs[from] || !tabs[from]->sync_input) return;
    for (i = 0; i < n; i++) {
        if (i == from || !tabs[i] || !tabs[i]->sync_input) continue;
        if (tabs[i]->state == YT_ST_CONNECTED)
            yt_session_write(tabs[i], data, len);
    }
}

static void (*g_sync_hook)(YtSession *, const char *, int);

void yt_app_set_sync_hook(void (*hook)(YtSession *from, const char *data, int len))
{
    g_sync_hook = hook;
}

void yt_app_sync_write(YtSession *from, const char *data, int len)
{
    if (g_sync_hook) g_sync_hook(from, data, len);
}
