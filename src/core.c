/* YTerminal - core.c : 纯逻辑（无 Win32 依赖，便于单元测试） */
#include "core.h"
#include <string.h>
#include <stdio.h>
#include <wchar.h>

static char lower_c(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

int yt_fuzzy_score(const char *needle, const char *hay) {
    if (!needle || !*needle) return 1;
    if (!hay || !*hay) return 0;
    int score = 100, consec = 0;
    const char *h = hay;
    for (const char *n = needle; *n; n++) {
        char nc = lower_c(*n);
        if (nc == ' ') { consec = 0; continue; }
        int found = 0;
        while (*h) {
            char prev = (h == hay) ? 0 : h[-1];
            char hc = lower_c(*h);
            h++;
            if (hc == nc) {
                found = 1;
                score += consec ? 8 : 2;
                if (h == hay + 1) score += 12;
                if (prev == 0 || prev == ' ' || prev == '/' || prev == '-' || prev == '.' || prev == '_')
                    score += 6;
                consec++;
                break;
            }
            consec = 0;
        }
        if (!found) return 0;
    }
    return score;
}

static int trim_copy(const char *src, char *dst, int cch) {
    if (!src) return 0;
    while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') src++;
    int n = (int)strlen(src);
    while (n > 0 && (src[n - 1] == ' ' || src[n - 1] == '\t' || src[n - 1] == '\r' || src[n - 1] == '\n')) n--;
    if (n >= cch) n = cch - 1;
    memcpy(dst, src, (size_t)n);
    dst[n] = 0;
    return n;
}

static void copy_s(char *dst, int cch, const char *src) {
    if (cch <= 0) return;
    _snprintf(dst, (size_t)cch, "%s", src ? src : "");
    dst[cch - 1] = 0;
}

static int port_ok(const char *s) {
    int v = 0, n = 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        v = v * 10 + (*s - '0');
        if (++n > 5) return 0;
        s++;
    }
    return n > 0 && v >= 1 && v <= 65535;
}

int yt_fwd_parse(const char *line, YtFwd *out) {
    if (!line || !out) return 0;
    char buf[256];
    if (trim_copy(line, buf, sizeof(buf)) == 0) return 0;

    char fields[4][128];
    int nf = 0;
    int pos = 0;
    if (buf[0] && buf[1] && (buf[0] == 'L' || buf[0] == 'l' || buf[0] == 'R' || buf[0] == 'r' ||
                             buf[0] == 'D' || buf[0] == 'd') && (buf[1] == ':' || buf[1] == '=')) {
        out->type = (buf[0] == 'R' || buf[0] == 'r') ? 1 : (buf[0] == 'D' || buf[0] == 'd') ? 2 : 0;
        pos = 2;
    } else {
        out->type = 0;
    }
    while (buf[pos] && nf < 4) {
        int k = 0;
        while (buf[pos] && buf[pos] != ':' && buf[pos] != '=' && k < 127) fields[nf][k++] = buf[pos++];
        fields[nf][k] = 0;
        if (buf[pos] == ':' || buf[pos] == '=') pos++;
        nf++;
    }
    memset(out->listen, 0, sizeof(out->listen));
    memset(out->host, 0, sizeof(out->host));
    memset(out->dest, 0, sizeof(out->dest));

    if (out->type == 2) {
        if (nf == 1) {
            if (!port_ok(fields[0])) return 0;
            copy_s(out->listen, sizeof(out->listen), fields[0]);
        } else if (nf == 2) {
            if (!port_ok(fields[1])) return 0;
            _snprintf(out->listen, sizeof(out->listen), "%s:%s", fields[0], fields[1]);
        } else return 0;
        out->type = 2;
        return 1;
    }
    if (nf == 3) {
        if (!port_ok(fields[0]) || !port_ok(fields[2]) || !fields[1][0]) return 0;
        copy_s(out->listen, sizeof(out->listen), fields[0]);
        copy_s(out->host, sizeof(out->host), fields[1]);
        copy_s(out->dest, sizeof(out->dest), fields[2]);
    } else if (nf == 4) {
        if (!port_ok(fields[1]) || !port_ok(fields[3]) || !fields[2][0]) return 0;
        _snprintf(out->listen, sizeof(out->listen), "%s:%s", fields[0], fields[1]);
        copy_s(out->host, sizeof(out->host), fields[2]);
        copy_s(out->dest, sizeof(out->dest), fields[3]);
    } else return 0;
    return 1;
}

int yt_fwd_format(const YtFwd *f, char *buf, int cch) {
    if (!f || !buf || cch < 8) return 0;
    static const char tag[] = "LRD";
    int t = (f->type >= 0 && f->type <= 2) ? f->type : 0;
    int n;
    if (t == 2) n = _snprintf(buf, (size_t)cch, "D:%s", f->listen);
    else n = _snprintf(buf, (size_t)cch, "%c:%s:%s:%s", tag[t], f->listen, f->host, f->dest);
    if (n < 0) { buf[0] = 0; return 0; }
    if (n >= cch) n = cch - 1;
    buf[n] = 0;
    return n;
}

static int hex_color(const char *s, u32 *out) {
    while (*s == ' ') s++;
    if (*s == '#') s++;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    u32 v = 0;
    int n = 0;
    while (*s) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        v = (v << 4) | (u32)d;
        if (++n == 6) break;
        s++;
    }
    if (n != 6) return 0;
    *out = 0x01000000u | v;
    return 1;
}

int yt_rules_parse(const char *src, YtRule *out, int max) {
    if (!src || !out || max <= 0) return 0;
    int n = 0;
    const char *p = src;
    while (*p && n < max) {
        const char *end = p;
        while (*end && *end != ';' && *end != '\n' && *end != '\r') end++;
        char seg[256];
        int len = (int)(end - p);
        if (len > 255) len = 255;
        memcpy(seg, p, (size_t)len);
        seg[len] = 0;
        p = (*end) ? end + 1 : end;

        char tmp[256];
        if (trim_copy(seg, tmp, sizeof(tmp)) == 0) continue;
        if (tmp[0] == '#' && !strstr(tmp, "=>")) continue;   /* 注释行 */
        char *arrow = strstr(tmp, "=>");
        if (!arrow) continue;
        *arrow = 0;
        char key[256];
        if (trim_copy(tmp, key, sizeof(key)) == 0) continue;
        u32 col;
        if (!hex_color(arrow + 2, &col)) col = 0x01FFB86Cu;
        if (strlen(key) >= sizeof(out[n].text)) key[sizeof(out[n].text) - 1] = 0;
        copy_s(out[n].text, sizeof(out[n].text), key);
        out[n].text[sizeof(out[n].text) - 1] = 0;
        out[n].color = col;
        n++;
    }
    return n;
}

static int wchar_cells(wchar_t c) {
    if (c < 0x1100) return 1;
    if ((c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) ||
        (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF) ||
        (c >= 0xFE30 && c <= 0xFE6F) || (c >= 0xFF00 && c <= 0xFF60))
        return 2;
    return 1;
}

void yt_tab_shorten(const wchar_t *in, int max_cells, wchar_t *out, int cch) {
    if (!out || cch <= 0) return;
    out[0] = 0;
    if (!in) return;
    int cells = 0, k = 0;
    const wchar_t *p = in;
    while (*p) {
        int cw = wchar_cells(*p);
        if (cells + cw > max_cells) break;
        if (k >= cch - 2) break;
        out[k++] = *p;
        cells += cw;
        p++;
    }
    if (p && *p && k > 0) {
        /* 留一个单元格给省略号 */
        while (k > 0 && cells + 1 > max_cells) {
            cells -= wchar_cells(out[k - 1]);
            k--;
        }
        if (k < cch - 1) out[k++] = 0x2026; /* … */
    }
    out[k] = 0;
}
