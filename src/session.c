#include "yt.h"

void yt_session_init(YtSession *s)
{
    ZeroMemory(s, sizeof(*s));
    InitializeCriticalSection(&s->lock);
    s->port = 22;
    s->baud = 115200;
    wcscpy(s->shell, L"powershell");
    wcscpy(s->com, L"COM3");
    s->state = YT_ST_IDLE;
}

void yt_session_free(YtSession *s)
{
    yt_session_close(s);
    if (s->log_fp) { fclose(s->log_fp); s->log_fp = NULL; }
    DeleteCriticalSection(&s->lock);
}

void yt_session_write(YtSession *s, const char *data, int len)
{
    if (!s || !s->write_fn || s->state != YT_ST_CONNECTED) return;
    EnterCriticalSection(&s->lock);
    s->write_fn(s, data, len);
    LeaveCriticalSection(&s->lock);
}

void yt_session_close(YtSession *s)
{
    if (!s) return;
    EnterCriticalSection(&s->lock);
    if (s->close_fn) s->close_fn(s);
    s->state = YT_ST_CLOSED;
    LeaveCriticalSection(&s->lock);
}

/* minimal JSON-ish session store (no cJSON dependency in release path) */
BOOL yt_sessions_save(const wchar_t *path, YtSession **tabs, int n)
{
    FILE *fp;
    char upath[YT_PATH_MAX];
    int i;
    WideCharToMultiByte(CP_UTF8, 0, path, -1, upath, sizeof(upath), NULL, NULL);
    fp = fopen(upath, "wb");
    if (!fp) return FALSE;
    fputs("[\n", fp);
    for (i = 0; i < n; i++) {
        YtSession *s = tabs[i];
        char host[128] = {0}, user[64] = {0}, com[32] = {0}, shell[64] = {0};
        if (!s) continue;
        WideCharToMultiByte(CP_UTF8, 0, s->host, -1, host, sizeof(host), NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, s->user, -1, user, sizeof(user), NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, s->com, -1, com, sizeof(com), NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, s->shell, -1, shell, sizeof(shell), NULL, NULL);
        fprintf(fp,
                "  {\"kind\":%d,\"host\":\"%s\",\"port\":%d,\"user\":\"%s\",\"com\":\"%s\",\"baud\":%d,\"shell\":\"%s\"}%s\n",
                (int)s->kind, host, s->port, user, com, s->baud, shell,
                (i + 1 < n) ? "," : "");
    }
    fputs("]\n", fp);
    fclose(fp);
    return TRUE;
}

static void wcopy_field(wchar_t *dst, int cap, const char *json, const char *key)
{
    char pat[64];
    const char *p;
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    p = strstr(json, pat);
    if (!p) { dst[0] = 0; return; }
    p += strlen(pat);
    {
        char tmp[128];
        int i = 0;
        while (*p && *p != '"' && i < (int)sizeof(tmp) - 1) tmp[i++] = *p++;
        tmp[i] = 0;
        MultiByteToWideChar(CP_UTF8, 0, tmp, -1, dst, cap);
    }
}

static int int_field(const char *json, const char *key, int defv)
{
    char pat[64];
    const char *p;
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    p = strstr(json, pat);
    if (!p) return defv;
    return atoi(p + strlen(pat));
}

int yt_sessions_load(const wchar_t *path, YtSession *out, int cap)
{
    FILE *fp;
    char *buf;
    long sz;
    int n = 0;
    char upath[YT_PATH_MAX];
    char *p, *start;

    WideCharToMultiByte(CP_UTF8, 0, path, -1, upath, sizeof(upath), NULL, NULL);
    fp = fopen(upath, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(fp); return 0; }
    fread(buf, 1, sz, fp); buf[sz] = 0; fclose(fp);

    p = buf;
    while (n < cap && (start = strchr(p, '{')) != NULL) {
        char *end = strchr(start, '}');
        char obj[512];
        int len;
        if (!end) break;
        len = (int)(end - start + 1);
        if (len >= (int)sizeof(obj)) len = sizeof(obj) - 1;
        memcpy(obj, start, len); obj[len] = 0;
        yt_session_init(&out[n]);
        out[n].kind = (YtKind)int_field(obj, "kind", 0);
        out[n].port = int_field(obj, "port", 22);
        out[n].baud = int_field(obj, "baud", 115200);
        wcopy_field(out[n].host, 128, obj, "host");
        wcopy_field(out[n].user, 64, obj, "user");
        wcopy_field(out[n].com, 32, obj, "com");
        wcopy_field(out[n].shell, 64, obj, "shell");
        n++;
        p = end + 1;
    }
    free(buf);
    return n;
}
