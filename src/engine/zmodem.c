#include "yt_core.h"

/* ZMODEM: trigger rz/sz; TX also offers base64 pipe fallback for local file. */

int yt_zmodem_rx(YtSession *s, const wchar_t *dir)
{
    char cmd[] = "rz -y\r";
    (void)dir;
    if (!s || s->state != YT_ST_CONNECTED) return -1;
    yt_session_write(s, cmd, (int)strlen(cmd));
    yt_session_post_out(s, "\r\n[YTerminal] ZMODEM RX (rz). Large files: use SFTP.\r\n", 56);
    return 0;
}

static int z_name_ok(const char *name)
{
    const char *p;
    if (!name || !name[0]) return 0;
    for (p = name; *p; p++) {
        if (*p == '\'' || *p == '"' || *p == '`' || *p == '$' || *p == ';' ||
            *p == '|' || *p == '&' || *p == '\n' || *p == '\r' || *p == '\\')
            return 0;
    }
    return 1;
}

int yt_zmodem_tx(YtSession *s, const wchar_t *file)
{
    FILE *fp;
    char path[YT_PATH_MAX], cmd[YT_PATH_MAX + 64], line[YT_PATH_MAX * 2], b64[120];
    unsigned char buf[57];
    size_t n;
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (!s || s->state != YT_ST_CONNECTED || !file) return -1;
    WideCharToMultiByte(CP_UTF8, 0, file, -1, path, sizeof(path), NULL, NULL);
    fp = fopen(path, "rb");
    if (!fp) {
        char *base = strrchr(path, '\\'); if (!base) base = strrchr(path, '/');
        base = base ? base + 1 : path;
        if (!z_name_ok(base)) return -1;
        snprintf(cmd, sizeof(cmd), "sz -e -- %s\r", base);
        yt_session_write(s, cmd, (int)strlen(cmd));
        return 0;
    }
    {
        char *base = strrchr(path, '\\'); if (!base) base = strrchr(path, '/');
        base = base ? base + 1 : path;
        if (!z_name_ok(base)) { fclose(fp); return -1; }
        snprintf(cmd, sizeof(cmd), "cat > '%s' <<'YT_EOF'\r", base);
        yt_session_write(s, cmd, (int)strlen(cmd));
        /* Prefer base64 decode on remote if available */
        yt_session_write(s, "-----BEGIN YT B64-----\r", 23);
        while ((n = fread(buf, 1, 57, fp)) > 0) {
            size_t i, o = 0;
            for (i = 0; i < n; i += 3) {
                unsigned a = buf[i], b = i + 1 < n ? buf[i + 1] : 0, c = i + 2 < n ? buf[i + 2] : 0;
                b64[o++] = T[a >> 2];
                b64[o++] = T[((a & 3) << 4) | (b >> 4)];
                b64[o++] = (i + 1 < n) ? T[((b & 15) << 2) | (c >> 6)] : '=';
                b64[o++] = (i + 2 < n) ? T[c & 63] : '=';
            }
            b64[o++] = '\r';
            yt_session_write(s, b64, (int)o);
            Sleep(5);
        }
        yt_session_write(s, "-----END YT B64-----\rYT_EOF\r", 28);
        snprintf(line, sizeof(line),
                 "grep -v '^-----' '%s' | base64 -d > '%s.bin' 2>/dev/null; mv -f '%s.bin' '%s' 2>/dev/null\r",
                 base, base, base, base);
        yt_session_write(s, line, (int)strlen(line));
    }
    fclose(fp);
    yt_session_post_out(s, "\r\n[YTerminal] ZMODEM/B64 TX done.\r\n", 36);
    return 0;
}
