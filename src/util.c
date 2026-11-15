#include "util.h"
#include <shlobj.h>
#include <stdarg.h>
#include <ctype.h>

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { MessageBoxW(NULL, L"内存分配失败", YT_APP_NAME, MB_ICONERROR); ExitProcess(1); }
    return p;
}
void *xcalloc(size_t n, size_t sz) {
    void *p = calloc(n ? n : 1, sz ? sz : 1);
    if (!p) { MessageBoxW(NULL, L"内存分配失败", YT_APP_NAME, MB_ICONERROR); ExitProcess(1); }
    return p;
}
void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) { MessageBoxW(NULL, L"内存分配失败", YT_APP_NAME, MB_ICONERROR); ExitProcess(1); }
    return q;
}
char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)xmalloc(n);
    memcpy(p, s, n);
    return p;
}
wchar_t *xwcsdup(const wchar_t *s) {
    if (!s) return NULL;
    size_t n = (wcslen(s) + 1) * sizeof(wchar_t);
    wchar_t *p = (wchar_t *)xmalloc(n);
    memcpy(p, s, n);
    return p;
}

/* ---------------- StrBuf ---------------- */
void sb_init(StrBuf *sb) { sb->cap = 256; sb->len = 0; sb->buf = (char *)xmalloc(sb->cap); sb->buf[0] = 0; }
void sb_free(StrBuf *sb) { free(sb->buf); sb->buf = NULL; sb->len = sb->cap = 0; }
static void sb_reserve(StrBuf *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) return;
    while (sb->cap < sb->len + extra + 1) sb->cap *= 2;
    sb->buf = (char *)xrealloc(sb->buf, sb->cap);
}
void sb_append_n(StrBuf *sb, const char *s, size_t n) {
    if (!s) return;
    sb_reserve(sb, n);
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = 0;
}
void sb_append(StrBuf *sb, const char *s) { if (s) sb_append_n(sb, s, strlen(s)); }
void sb_appendf(StrBuf *sb, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char tmp[1024];
    int n = _vsnprintf(tmp, sizeof(tmp) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) n = (int)sizeof(tmp) - 1;
    sb_append_n(sb, tmp, (size_t)n);
}
void sb_appendw(StrBuf *sb, const wchar_t *s) {
    if (!s) return;
    int cch = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    if (cch <= 1) return;
    char *tmp = (char *)xmalloc((size_t)cch);
    WideCharToMultiByte(CP_UTF8, 0, s, -1, tmp, cch, NULL, NULL);
    sb_append_n(sb, tmp, (size_t)(cch - 1));
    free(tmp);
}
void sb_clear(StrBuf *sb) { sb->len = 0; sb->buf[0] = 0; }
char *sb_detach(StrBuf *sb) {
    char *p = sb->buf;
    sb->buf = NULL; sb->len = sb->cap = 0;
    return p;
}

/* ---------------- UTF-8 ---------------- */
wchar_t *utf8_to_wide(const char *s) {
    if (!s) return NULL;
    int cch = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (cch <= 0) { wchar_t *w = (wchar_t *)xmalloc(sizeof(wchar_t)); w[0] = 0; return w; }
    wchar_t *w = (wchar_t *)xmalloc((size_t)cch * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, cch);
    return w;
}
char *wide_to_utf8(const wchar_t *s) {
    if (!s) return NULL;
    int cch = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    if (cch <= 0) { char *c = (char *)xmalloc(1); c[0] = 0; return c; }
    char *c = (char *)xmalloc((size_t)cch);
    WideCharToMultiByte(CP_UTF8, 0, s, -1, c, cch, NULL, NULL);
    return c;
}
int utf8_decode_one(const unsigned char *s, size_t len, u32 *cp) {
    if (!len) return 0;
    unsigned char c = s[0];
    if (c < 0x80) { if (cp) *cp = c; return 1; }
    if (c < 0xC2) return 0;
    if ((c & 0xE0) == 0xC0) {
        if (len < 2 || (s[1] & 0xC0) != 0x80) return 0;
        u32 v = ((u32)(c & 0x1F) << 6) | (s[1] & 0x3F);
        if (v < 0x80) return 0;
        if (cp) *cp = v;
        return 2;
    }
    if ((c & 0xF0) == 0xE0) {
        if (len < 3 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return 0;
        u32 v = ((u32)(c & 0x0F) << 12) | ((u32)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        if (v < 0x800 || (v >= 0xD800 && v <= 0xDFFF)) return 0;
        if (cp) *cp = v;
        return 3;
    }
    if ((c & 0xF8) == 0xF0) {
        if (len < 4 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return 0;
        u32 v = ((u32)(c & 0x07) << 18) | ((u32)(s[1] & 0x3F) << 12) | ((u32)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        if (v < 0x10000 || v > 0x10FFFF) return 0;
        if (cp) *cp = v;
        return 4;
    }
    return 0;
}
size_t utf8_encode_one(u32 cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}
int ucs_width(u32 cp) {
    if (cp == 0) return 0;
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return 0;
    if (cp < 0x300) return 1;
    /* 零宽：组合符号、变体选择符、零宽连接符 */
    if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0483 && cp <= 0x0489) ||
        (cp >= 0x0591 && cp <= 0x05BD) || (cp >= 0x0610 && cp <= 0x061A) ||
        (cp >= 0x064B && cp <= 0x065F) || (cp >= 0x0670 && cp <= 0x0670) ||
        (cp >= 0x06D6 && cp <= 0x06DC) || (cp >= 0x0730 && cp <= 0x074A) ||
        (cp >= 0x07A6 && cp <= 0x07B0) || (cp >= 0x0900 && cp <= 0x0903) ||
        (cp >= 0x093A && cp <= 0x094F) || (cp >= 0x0E31 && cp <= 0x0E3A) ||
        (cp >= 0x0E47 && cp <= 0x0E4E) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
        (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20F0) ||
        (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xFE20 && cp <= 0xFE2F) ||
        (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x2060 && cp <= 0x2064) ||
        (cp >= 0xFEFF && cp <= 0xFEFF)) return 0;
    /* 宽字符（东亚宽/表情） */
    if (cp >= 0x1100 && cp <= 0x115F) return 2;
    if (cp >= 0x2E80 && cp <= 0x303E) return 2;
    if (cp >= 0x3041 && cp <= 0x33FF) return 2;
    if (cp >= 0x3400 && cp <= 0x4DBF) return 2;
    if (cp >= 0x4E00 && cp <= 0x9FFF) return 2;
    if (cp >= 0xA000 && cp <= 0xA4CF) return 2;
    if (cp >= 0xA960 && cp <= 0xA97F) return 2;
    if (cp >= 0xAC00 && cp <= 0xD7A3) return 2;
    if (cp >= 0xF900 && cp <= 0xFAFF) return 2;
    if (cp >= 0xFE10 && cp <= 0xFE19) return 2;
    if (cp >= 0xFE30 && cp <= 0xFE6F) return 2;
    if (cp >= 0xFF00 && cp <= 0xFF60) return 2;
    if (cp >= 0xFFE0 && cp <= 0xFFE6) return 2;
    if (cp >= 0x1F000 && cp <= 0x1FAFF) return 2;
    if (cp >= 0x20000 && cp <= 0x3FFFD) return 2;
    return 1;
}

/* ---------------- 路径 ---------------- */
void yt_app_dir(wchar_t *out, int cch) {
    wchar_t base[MAX_PATH] = {0};
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, base);
    _snwprintf(out, (size_t)cch, L"%s\\YTerminal", base);
    out[cch - 1] = 0;
}
void yt_exe_dir(wchar_t *out, int cch) {
    GetModuleFileNameW(NULL, out, (DWORD)cch);
    wchar_t *p = wcsrchr(out, L'\\');
    if (p) *p = 0;
}
static BOOL mkdir_one(const wchar_t *p) {
    if (CreateDirectoryW(p, NULL)) return TRUE;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL yt_mkdir(const wchar_t *path) {
    wchar_t tmp[MAX_PATH * 2];
    wcsncpy(tmp, path, MAX_PATH * 2 - 1);
    tmp[MAX_PATH * 2 - 1] = 0;
    for (wchar_t *p = tmp; *p; p++) {
        if (*p == L'\\' || *p == L'/') {
            wchar_t c = *p; *p = 0;
            if (wcslen(tmp) > 2) mkdir_one(tmp);
            *p = c;
        }
    }
    return mkdir_one(tmp);
}
void yt_ensure_dirs(void) {
    wchar_t app[MAX_PATH], d[MAX_PATH];
    yt_app_dir(app, MAX_PATH); yt_mkdir(app);
    _snwprintf(d, MAX_PATH, L"%s\\logs", app); yt_mkdir(d);
    _snwprintf(d, MAX_PATH, L"%s\\keys", app); yt_mkdir(d);
}
BOOL yt_file_exists(const wchar_t *path) {
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
BOOL yt_write_file(const wchar_t *path, const void *data, size_t len) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, data, (DWORD)len, &wrote, NULL) && wrote == (DWORD)len;
    CloseHandle(h);
    return ok;
}
char *yt_read_file(const wchar_t *path, size_t *out_len) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER sz; sz.QuadPart = 0;
    GetFileSizeEx(h, &sz);
    size_t n = (size_t)sz.QuadPart;
    char *buf = (char *)xmalloc(n + 1);
    DWORD got = 0;
    if (!ReadFile(h, buf, (DWORD)n, &got, NULL)) { CloseHandle(h); free(buf); return NULL; }
    CloseHandle(h);
    buf[got] = 0;
    if (out_len) *out_len = got;
    return buf;
}

/* ---------------- 时间 ---------------- */
static void fmt_time(wchar_t *out, int cch, const wchar_t *fmt, SYSTEMTIME *st) {
    wchar_t tmp[128];
    _snwprintf(tmp, 128, fmt, (int)st->wYear, (int)st->wMonth, (int)st->wDay,
               (int)st->wHour, (int)st->wMinute, (int)st->wSecond);
    wcsncpy(out, tmp, (size_t)cch - 1);
    out[cch - 1] = 0;
}
void yt_now_local_str(wchar_t *out, int cch, const wchar_t *fmt) {
    SYSTEMTIME st; GetLocalTime(&st);
    fmt_time(out, cch, fmt, &st);
}
void yt_now_time_str(wchar_t *out, int cch) {
    SYSTEMTIME st; GetLocalTime(&st);
    _snwprintf(out, (size_t)cch, L"%02d:%02d:%02d", (int)st.wHour, (int)st.wMinute, (int)st.wSecond);
    out[cch - 1] = 0;
}
void yt_now_date_str(wchar_t *out, int cch) {
    SYSTEMTIME st; GetLocalTime(&st);
    _snwprintf(out, (size_t)cch, L"%04d-%02d-%02d", (int)st.wYear, (int)st.wMonth, (int)st.wDay);
    out[cch - 1] = 0;
}
void yt_now_stamp(wchar_t *out, int cch) {
    SYSTEMTIME st; GetLocalTime(&st);
    _snwprintf(out, (size_t)cch, L"%04d%02d%02d_%02d%02d%02d",
               (int)st.wYear, (int)st.wMonth, (int)st.wDay,
               (int)st.wHour, (int)st.wMinute, (int)st.wSecond);
    out[cch - 1] = 0;
}

/* ---------------- 其他 ---------------- */
int yt_clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
void yt_trim(char *s) {
    if (!s) return;
    char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
}
BOOL yt_contains_ci(const char *hay, const char *needle) {
    if (!hay || !needle) return FALSE;
    if (!*needle) return TRUE;
    size_t n = strlen(needle);
    for (const char *p = hay; *p; p++) {
        if (_strnicmp(p, needle, n) == 0) return TRUE;
    }
    return FALSE;
}
void yt_basename(const wchar_t *path, wchar_t *out, int cch) {
    const wchar_t *p = wcsrchr(path, L'\\');
    if (!p) p = wcsrchr(path, L'/');
    p = p ? p + 1 : path;
    wcsncpy(out, p, (size_t)cch - 1); out[cch - 1] = 0;
}
/* ---- 会话编码 ---- */
int yt_enc_codepage(int enc) {
    switch (enc) {
    case YT_ENC_GBK:   return 936;
    case YT_ENC_BIG5:  return 950;
    case YT_ENC_SJIS:  return 932;
    case YT_ENC_EUCKR: return 949;
    default:           return CP_UTF8;
    }
}

const wchar_t *yt_enc_name(int enc) {
    switch (enc) {
    case YT_ENC_GBK:   return L"GBK";
    case YT_ENC_BIG5:  return L"BIG5";
    case YT_ENC_SJIS:  return L"Shift-JIS";
    case YT_ENC_EUCKR: return L"EUC-KR";
    default:           return L"UTF-8";
    }
}

int yt_enc_is_lead(int enc, unsigned char b) {
    switch (enc) {
    case YT_ENC_GBK:   return b >= 0x81 && b <= 0xFE;
    case YT_ENC_BIG5:  return b >= 0x81 && b <= 0xFE;
    case YT_ENC_SJIS:  return (b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC);
    case YT_ENC_EUCKR: return b >= 0x81 && b <= 0xFE;
    default:           return 0;
    }
}

char *yt_bytes_to_utf8(const char *data, size_t n, int enc, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!data || !n) return NULL;
    int cp = yt_enc_codepage(enc);
    if (cp == CP_UTF8) {
        char *d = (char *)xmalloc(n + 1);
        memcpy(d, data, n); d[n] = 0;
        if (out_len) *out_len = n;
        return d;
    }
    int wlen = MultiByteToWideChar(cp, 0, data, (int)n, NULL, 0);
    if (wlen <= 0) {
        /* 无法按目标码页解码时原样透传，避免整段丢失 */
        char *d = (char *)xmalloc(n + 1);
        memcpy(d, data, n); d[n] = 0;
        if (out_len) *out_len = n;
        return d;
    }
    wchar_t *w = (wchar_t *)xmalloc(sizeof(wchar_t) * (size_t)(wlen + 1));
    MultiByteToWideChar(cp, 0, data, (int)n, w, wlen);
    w[wlen] = 0;
    int ulen = WideCharToMultiByte(CP_UTF8, 0, w, wlen, NULL, 0, NULL, NULL);
    char *u = (char *)xmalloc((size_t)(ulen > 0 ? ulen : 0) + 1);
    if (ulen > 0) WideCharToMultiByte(CP_UTF8, 0, w, wlen, u, ulen, NULL, NULL);
    u[ulen > 0 ? ulen : 0] = 0;
    free(w);
    if (out_len) *out_len = (size_t)(ulen > 0 ? ulen : 0);
    return u;
}

char *yt_utf8_to_bytes(const char *data, size_t n, int enc, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!data || !n) return NULL;
    int cp = yt_enc_codepage(enc);
    if (cp == CP_UTF8) {
        char *d = (char *)xmalloc(n + 1);
        memcpy(d, data, n); d[n] = 0;
        if (out_len) *out_len = n;
        return d;
    }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, data, (int)n, NULL, 0);
    if (wlen <= 0) {
        char *d = (char *)xmalloc(n + 1);
        memcpy(d, data, n); d[n] = 0;
        if (out_len) *out_len = n;
        return d;
    }
    wchar_t *w = (wchar_t *)xmalloc(sizeof(wchar_t) * (size_t)(wlen + 1));
    MultiByteToWideChar(CP_UTF8, 0, data, (int)n, w, wlen);
    w[wlen] = 0;
    int blen = WideCharToMultiByte(cp, 0, w, wlen, NULL, 0, NULL, NULL);
    char *b = (char *)xmalloc((size_t)(blen > 0 ? blen : 0) + 1);
    if (blen > 0) WideCharToMultiByte(cp, 0, w, wlen, b, blen, NULL, NULL);
    b[blen > 0 ? blen : 0] = 0;
    free(w);
    if (out_len) *out_len = (size_t)(blen > 0 ? blen : 0);
    return b;
}

BOOL yt_wildcard_match(const char *pattern, const char *text) {
    if (!pattern) return TRUE;
    if (!*pattern) return !*text;
    if (*pattern == '*') {
        while (*pattern == '*') pattern++;
        if (!*pattern) return TRUE;
        for (const char *t = text; ; t++) {
            if (yt_wildcard_match(pattern, t)) return TRUE;
            if (!*t) break;
        }
        return FALSE;
    }
    if (!*text) return FALSE;
    if (*pattern == '?' || tolower((unsigned char)*pattern) == tolower((unsigned char)*text))
        return yt_wildcard_match(pattern + 1, text + 1);
    return FALSE;
}
