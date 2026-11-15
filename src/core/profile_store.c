#include "yt_core.h"

static void esc(FILE *fp, const wchar_t *w)
{
    char u[YT_HOST_MAX * 3];
    int i;
    WideCharToMultiByte(CP_UTF8, 0, w ? w : L"", -1, u, sizeof(u), NULL, NULL);
    fputc('"', fp);
    for (i = 0; u[i]; i++) {
        if (u[i] == '"' || u[i] == '\\') fputc('\\', fp);
        fputc(u[i], fp);
    }
    fputc('"', fp);
}

int yt_profile_save(const YtSession *list, int n)
{
    FILE *fp;
    char path[YT_PATH_MAX];
    int i;
    WideCharToMultiByte(CP_UTF8, 0, yt_paths()->sessions, -1, path, sizeof(path), NULL, NULL);
    fp = fopen(path, "wb");
    if (!fp) return 0;
    fputs("[\n", fp);
    for (i = 0; i < n; i++) {
        const YtSession *s = &list[i];
        fprintf(fp, "  {\"kind\":%d,\"port\":%d,\"baud\":%d,\"jump_port\":%d,",
                (int)s->kind, s->port, s->baud, s->jump_port);
        fputs("\"name\":", fp); esc(fp, s->name);
        fputs(",\"group\":", fp); esc(fp, s->group);
        fputs(",\"host\":", fp); esc(fp, s->host);
        fputs(",\"user\":", fp); esc(fp, s->user);
        fputs(",\"com\":", fp); esc(fp, s->com);
        fputs(",\"shell\":", fp); esc(fp, s->shell);
        fputs(",\"keyfile\":", fp); esc(fp, s->keyfile);
        fputs(",\"jump_host\":", fp); esc(fp, s->jump_host);
        fputs(",\"jump_user\":", fp); esc(fp, s->jump_user);
        fputs(",\"forwards\":", fp); {
            wchar_t fw[512];
            MultiByteToWideChar(CP_UTF8, 0, s->forwards, -1, fw, 512);
            esc(fp, fw);
        }
        fprintf(fp, "}%s\n", i + 1 < n ? "," : "");
        if (s->pass[0]) {
            wchar_t cid[320];
            swprintf(cid, 320, L"%ls@%ls:%d", s->user, s->host, s->port);
            yt_cred_save(cid, s->pass);
        }
    }
    fputs("]\n", fp);
    fclose(fp);
    return n;
}

static const char *find_key(const char *obj, const char *key)
{
    char pat[80];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    return strstr(obj, pat);
}

static void get_str(const char *obj, const char *key, wchar_t *dst, int cap)
{
    const char *p = find_key(obj, key);
    char tmp[YT_HOST_MAX];
    int i = 0;
    dst[0] = 0;
    if (!p) return;
    p = strchr(p + 1, ':');
    if (!p) return;
    p++;
    while (*p == ' ') p++;
    if (*p != '"') return;
    p++;
    while (*p && *p != '"' && i < (int)sizeof(tmp) - 1) {
        if (*p == '\\' && p[1]) p++;
        tmp[i++] = *p++;
    }
    tmp[i] = 0;
    MultiByteToWideChar(CP_UTF8, 0, tmp, -1, dst, cap);
}

static int get_int(const char *obj, const char *key, int defv)
{
    const char *p = find_key(obj, key);
    if (!p) return defv;
    p = strchr(p + 1, ':');
    if (!p) return defv;
    return atoi(p + 1);
}

int yt_profile_load(YtSession *out, int cap)
{
    FILE *fp;
    char path[YT_PATH_MAX], *buf, *start, *end;
    long sz;
    int n = 0;
    WideCharToMultiByte(CP_UTF8, 0, yt_paths()->sessions, -1, path, sizeof(path), NULL, NULL);
    fp = fopen(path, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(fp); return 0; }
    fread(buf, 1, sz, fp); buf[sz] = 0; fclose(fp);
    start = buf;
    while (n < cap && (start = strchr(start, '{')) != NULL) {
        char obj[2048];
        int len;
        end = strchr(start, '}');
        if (!end) break;
        len = (int)(end - start + 1);
        if (len >= (int)sizeof(obj)) len = sizeof(obj) - 1;
        memcpy(obj, start, len); obj[len] = 0;
        yt_session_init(&out[n]);
        out[n].kind = (YtKind)get_int(obj, "kind", 0);
        out[n].port = get_int(obj, "port", 22);
        out[n].baud = get_int(obj, "baud", 115200);
        out[n].jump_port = get_int(obj, "jump_port", 22);
        get_str(obj, "name", out[n].name, YT_TITLE_MAX);
        get_str(obj, "group", out[n].group, 64);
        get_str(obj, "host", out[n].host, YT_HOST_MAX);
        get_str(obj, "user", out[n].user, 64);
        get_str(obj, "com", out[n].com, 32);
        get_str(obj, "shell", out[n].shell, 64);
        get_str(obj, "keyfile", out[n].keyfile, YT_PATH_MAX);
        get_str(obj, "jump_host", out[n].jump_host, YT_HOST_MAX);
        get_str(obj, "jump_user", out[n].jump_user, 64);
        {
            wchar_t fw[512];
            char f8[512];
            get_str(obj, "forwards", fw, 512);
            WideCharToMultiByte(CP_UTF8, 0, fw, -1, f8, sizeof(f8), NULL, NULL);
            strncpy(out[n].forwards, f8, sizeof(out[n].forwards) - 1);
        }
        {
            wchar_t cid[320];
            swprintf(cid, 320, L"%ls@%ls:%d", out[n].user, out[n].host, out[n].port);
            yt_cred_load(cid, out[n].pass, 128);
        }
        if (!out[n].name[0]) swprintf(out[n].name, YT_TITLE_MAX, L"%ls@%ls", out[n].user, out[n].host);
        n++;
        start = end + 1;
    }
    free(buf);
    return n;
}

static void fill_ssh(YtSession *s, const wchar_t *name, const wchar_t *group,
                     const wchar_t *host, int port, const wchar_t *user, const wchar_t *pass)
{
    yt_session_init(s);
    s->kind = YT_KIND_SSH;
    wcsncpy(s->name, name, YT_TITLE_MAX - 1);
    wcsncpy(s->group, group, 63);
    wcsncpy(s->host, host, YT_HOST_MAX - 1);
    s->port = port;
    wcsncpy(s->user, user, 63);
    wcsncpy(s->pass, pass, 127);
}

static void fill_serial(YtSession *s, const wchar_t *name, const wchar_t *group,
                        const wchar_t *com, int baud, const wchar_t *user, const wchar_t *pass)
{
    yt_session_init(s);
    s->kind = YT_KIND_SERIAL;
    wcsncpy(s->name, name, YT_TITLE_MAX - 1);
    wcsncpy(s->group, group, 63);
    wcsncpy(s->com, com, 31);
    s->baud = baud;
    if (user) wcsncpy(s->user, user, 63);
    if (pass) wcsncpy(s->pass, pass, 127);
}

int yt_profile_ensure_defaults(YtSession *out, int cap)
{
    int n = 0;
    if (!out || cap < 3) return 0;
    /* 无真实凭据；密码留空，由用户自行填写（S-03） */
    fill_ssh(&out[n++], L"示例 SSH", L"SSH", L"127.0.0.1", 22, L"", L"");
    fill_serial(&out[n++], L"示例串口 COM3", L"串口", L"COM3", 115200, NULL, NULL);
    {
        yt_session_init(&out[n]);
        out[n].kind = YT_KIND_LOCAL;
        wcscpy(out[n].name, L"本地 PowerShell");
        wcscpy(out[n].group, L"本地");
        wcscpy(out[n].shell, L"powershell");
        n++;
    }
    yt_profile_save(out, n);
    return n;
}
