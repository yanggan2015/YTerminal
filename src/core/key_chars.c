#include "key_chars.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>

static int emit(char *out, int cap, int *n, const char *s, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        if (*n + 1 >= cap) return 0;
        out[(*n)++] = s[i];
    }
    return 1;
}

static int named(const char *k, char *tmp, int tcap)
{
    if (!strcmp(k, "enter") || !strcmp(k, "return") || !strcmp(k, "cr")) {
        tmp[0] = '\r'; return 1;
    }
    if (!strcmp(k, "lf") || !strcmp(k, "newline")) { tmp[0] = '\n'; return 1; }
    if (!strcmp(k, "tab")) { tmp[0] = '\t'; return 1; }
    if (!strcmp(k, "esc") || !strcmp(k, "escape")) { tmp[0] = 0x1b; return 1; }
    if (!strcmp(k, "space")) { tmp[0] = ' '; return 1; }
    if (!strcmp(k, "backspace") || !strcmp(k, "bs")) { tmp[0] = 0x7f; return 1; }
    if (!strcmp(k, "up")) { memcpy(tmp, "\x1b[A", 3); return 3; }
    if (!strcmp(k, "down")) { memcpy(tmp, "\x1b[B", 3); return 3; }
    if (!strcmp(k, "right")) { memcpy(tmp, "\x1b[C", 3); return 3; }
    if (!strcmp(k, "left")) { memcpy(tmp, "\x1b[D", 3); return 3; }
    if (!strncmp(k, "ctrl+", 5) && k[5] && !k[6]) {
        tmp[0] = (char)(toupper((unsigned char)k[5]) & 0x1f);
        return 1;
    }
    if (!strncmp(k, "control+", 8) && k[8] && !k[9]) {
        tmp[0] = (char)(toupper((unsigned char)k[8]) & 0x1f);
        return 1;
    }
    (void)tcap;
    return 0;
}

int yt_keys_expand(const char *spec, char *out, int cap)
{
    int n = 0;
    const char *p;
    if (!spec || !out || cap < 2) return 0;
    p = spec;
    while (*p) {
        if (*p == '#' && !strncmp(p, "#delay:", 7)) {
            int ms = atoi(p + 7);
            while (*p && *p != '<' && *p != '#' && (unsigned char)*p > 32) p++;
            if (ms > 0) {
                if (ms > 5000) ms = 5000;
                Sleep((DWORD)ms);
            }
            continue;
        }
        if (*p == '<') {
            const char *e = strchr(p, '>');
            char key[64], tmp[16];
            int kn, m;
            if (!e) break;
            kn = (int)(e - p - 1);
            if (kn <= 0 || kn >= (int)sizeof(key)) { p = e + 1; continue; }
            memcpy(key, p + 1, (size_t)kn); key[kn] = 0;
            for (m = 0; key[m]; m++) key[m] = (char)tolower((unsigned char)key[m]);
            m = named(key, tmp, (int)sizeof(tmp));
            if (m > 0) emit(out, cap, &n, tmp, m);
            p = e + 1;
            continue;
        }
        if (!emit(out, cap, &n, p, 1)) break;
        p++;
    }
    out[n] = 0;
    return n;
}
