#include "yt_core.h"

/* config/window.json — 窗口/工作台配置（与 sessions.json 分离） */

static int dig_int(const char *buf, const char *key, int defv)
{
    char pat[64];
    const char *p;
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    p = strstr(buf, pat);
    if (!p) return defv;
    return atoi(p + (int)strlen(pat));
}

BOOL yt_window_save(const YtWindowCfg *c)
{
    FILE *fp;
    char path[YT_PATH_MAX];
    if (!c) return FALSE;
    WideCharToMultiByte(CP_UTF8, 0, yt_paths()->window, -1, path, sizeof(path), NULL, NULL);
    fp = fopen(path, "wb");
    if (!fp) return FALSE;
    fprintf(fp,
            "{\n"
            "  \"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"maximized\":%d,\n"
            "  \"sm_visible\":%d,\"bb_visible\":%d,\"sftp_visible\":%d,\n"
            "  \"split\":%d,\"theme\":%d,\"font_px\":%d,\"opacity\":%d,\n"
            "  \"kind\":%d,\"stats\":%d,\"paste_delay_ms\":%d\n"
            "}\n",
            c->x, c->y, c->w, c->h, c->maximized ? 1 : 0,
            c->sm_visible ? 1 : 0, c->bb_visible ? 1 : 0, c->sftp_visible ? 1 : 0,
            c->split, c->theme, c->font_px, c->opacity,
            c->kind, c->stats ? 1 : 0, c->paste_delay_ms);
    fclose(fp);
    return TRUE;
}

BOOL yt_window_load(YtWindowCfg *c)
{
    FILE *fp;
    char path[YT_PATH_MAX], buf[2048];
    size_t n;
    if (!c) return FALSE;
    ZeroMemory(c, sizeof(*c));
    c->w = 1280; c->h = 800; c->x = CW_USEDEFAULT; c->y = CW_USEDEFAULT;
    c->sm_visible = TRUE; c->bb_visible = TRUE; c->sftp_visible = FALSE;
    c->split = 0; c->theme = 0; c->font_px = 14; c->opacity = 255;
    c->kind = 1; c->stats = FALSE; c->paste_delay_ms = 0;
    WideCharToMultiByte(CP_UTF8, 0, yt_paths()->window, -1, path, sizeof(path), NULL, NULL);
    fp = fopen(path, "rb");
    if (!fp) return FALSE;
    n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = 0;
    fclose(fp);
    c->x = dig_int(buf, "x", c->x);
    c->y = dig_int(buf, "y", c->y);
    c->w = dig_int(buf, "w", c->w);
    c->h = dig_int(buf, "h", c->h);
    c->maximized = dig_int(buf, "maximized", 0) != 0;
    c->sm_visible = dig_int(buf, "sm_visible", 1) != 0;
    c->bb_visible = dig_int(buf, "bb_visible", 1) != 0;
    c->sftp_visible = dig_int(buf, "sftp_visible", 0) != 0;
    c->split = dig_int(buf, "split", 0);
    c->theme = dig_int(buf, "theme", 0);
    c->font_px = dig_int(buf, "font_px", 14);
    c->opacity = dig_int(buf, "opacity", 255);
    c->kind = dig_int(buf, "kind", 1);
    c->stats = dig_int(buf, "stats", 0) != 0;
    c->paste_delay_ms = dig_int(buf, "paste_delay_ms", 0);
    if (c->w < 640) c->w = 640;
    if (c->h < 480) c->h = 480;
    if (c->opacity < 180) c->opacity = 180;
    if (c->opacity > 255) c->opacity = 255;
    return TRUE;
}

/* 兼容旧 layout.json 菜单项 */
BOOL yt_layout_save(int ntabs, int cur, int split, const int *kinds, int nk)
{
    YtWindowCfg c;
    (void)ntabs; (void)cur; (void)kinds; (void)nk;
    yt_window_load(&c);
    c.split = split;
    return yt_window_save(&c);
}

BOOL yt_layout_load(int *ntabs, int *cur, int *split, int *kinds, int cap, int *nk)
{
    YtWindowCfg c;
    (void)kinds; (void)cap;
    if (!yt_window_load(&c)) return FALSE;
    if (ntabs) *ntabs = 0;
    if (cur) *cur = 0;
    if (split) *split = c.split;
    if (nk) *nk = 0;
    return TRUE;
}
