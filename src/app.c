#include "app.h"
#include "ids.h"
#include "ui_term.h"
#include "json.h"
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

App g_app;

/* ================================================================== */
/* 基础                                                                 */
/* ================================================================== */
int app_dpi(int px) {
    UINT d = g_app.dpi ? g_app.dpi : 96;
    return MulDiv(px, (int)d, 96);
}

Theme *app_theme(void) { return g_app.theme ? g_app.theme : theme_get(0); }

COLORREF app_cr(u32 color, u32 def) {
    (void)def;
    Theme *t = app_theme();
    if (color & 0xFF000000u) {
        if ((color & 0xFF000000u) == 0x01000000u)
            return RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
        return t->fg;
    }
    u32 rgb = theme_color_256(t, (int)(color & 0xFF));
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

COLORREF app_ui_bg(void) { return app_cr(app_theme()->ui_bg, 0); }
COLORREF app_ui_panel(void) { return app_cr(app_theme()->ui_panel, 0); }
COLORREF app_ui_border(void) { return app_cr(app_theme()->ui_border, 0); }
COLORREF app_ui_text(void) { return app_cr(app_theme()->ui_text, 0); }
COLORREF app_ui_dim(void) { return app_cr(app_theme()->ui_dim, 0); }
COLORREF app_ui_accent(void) { return app_cr(app_theme()->ui_accent, 0); }
COLORREF app_ui_hover(void) { return app_cr(app_theme()->ui_hover, 0); }

void app_apply_theme(void) {
    g_app.theme = theme_get(g_app.set.theme >= 0 && g_app.set.theme < theme_count() ? g_app.set.theme : theme_default());
    if (g_app.main) {
        InvalidateRect(g_app.main, NULL, TRUE);
        if (g_app.toolbar) InvalidateRect(g_app.toolbar, NULL, FALSE);
        if (g_app.tabbar) InvalidateRect(g_app.tabbar, NULL, FALSE);
        if (g_app.sidebar) InvalidateRect(g_app.sidebar, NULL, FALSE);
        if (g_app.statusbar) InvalidateRect(g_app.statusbar, NULL, FALSE);
        for (int i = 0; i < g_app.tab_count; i++) {
            HWND v = app_root_view(g_app.roots[i]);
            if (v) SendMessageW(v, WM_YT_APPLY_SETTINGS, 0, 0);
        }
    }
}

/* ================================================================== */
/* 字体                                                                 */
/* ================================================================== */
static HFONT mkfont(const wchar_t *face, int pt, int bold, int italic, int cjk) {
    LOGFONTW lf;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -MulDiv(pt, (int)(g_app.dpi ? g_app.dpi : 96), 72);
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = (BYTE)(italic ? 1 : 0);
    lf.lfCharSet = cjk ? GB2312_CHARSET : DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = cjk ? DEFAULT_PITCH | FF_DONTCARE : FIXED_PITCH | FF_MODERN;
    wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
    lf.lfFaceName[LF_FACESIZE - 1] = 0;
    return CreateFontIndirectW(&lf);
}

static int font_exists(const wchar_t *face) {
    HFONT f = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          FIXED_PITCH | FF_MODERN, face);
    if (!f) return 0;
    HDC dc = CreateCompatibleDC(NULL);
    HFONT old = (HFONT)SelectObject(dc, f);
    wchar_t got[LF_FACESIZE] = { 0 };
    GetTextFaceW(dc, LF_FACESIZE, got);
    int ok = wcscmp(got, face) == 0;
    SelectObject(dc, old);
    DeleteDC(dc);
    DeleteObject(f);
    return ok;
}

void app_rebuild_fonts(void) {
    HDC dc = GetDC(g_app.main);
    if (dc) g_app.dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSY);

    HFONT dels[] = { g_app.f_latin, g_app.f_latin_b, g_app.f_latin_i, g_app.f_latin_bi,
                     g_app.f_cjk, g_app.f_cjk_b, g_app.f_cjk_i, g_app.f_ui, g_app.f_ui_b };
    for (int i = 0; i < (int)(sizeof(dels) / sizeof(dels[0])); i++) if (dels[i]) DeleteObject(dels[i]);

    char *fn = g_app.set.font[0] ? g_app.set.font : "Cascadia Mono";
    wchar_t face[LF_FACESIZE] = { 0 };
    wchar_t *wf = utf8_to_wide(fn);
    wcsncpy(face, wf, LF_FACESIZE - 1);
    free(wf);
    if (!font_exists(face)) {
        wcscpy(face, L"Consolas");
        if (!font_exists(face)) wcscpy(face, L"Courier New");
    }
    int pt = g_app.set.font_size > 0 ? g_app.set.font_size : 11;
    int cjkpt = g_app.set.font_size > 0 ? g_app.set.font_size : 11;
    g_app.f_latin = mkfont(face, pt, 0, 0, 0);
    g_app.f_latin_b = mkfont(face, pt, 1, 0, 0);
    g_app.f_latin_i = mkfont(face, pt, 0, 1, 0);
    g_app.f_latin_bi = mkfont(face, pt, 1, 1, 0);
    g_app.f_cjk = mkfont(L"Microsoft YaHei UI", cjkpt, 0, 0, 1);
    g_app.f_cjk_b = mkfont(L"Microsoft YaHei UI", cjkpt, 1, 0, 1);
    g_app.f_cjk_i = g_app.f_cjk;
    g_app.f_ui = mkfont(L"Microsoft YaHei UI", 9, 0, 0, 1);
    g_app.f_ui_b = mkfont(L"Microsoft YaHei UI", 9, 1, 0, 1);

    if (dc) {
        HFONT old = (HFONT)SelectObject(dc, g_app.f_latin);
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        g_app.cell_w = tm.tmAveCharWidth;
        if (g_app.cell_w < 3) g_app.cell_w = tm.tmMaxCharWidth;
        g_app.cell_h = tm.tmHeight + g_app.set.line_pad;
        g_app.font_h = tm.tmHeight;
        g_app.font_ascent = tm.tmAscent;
        g_app.font_pad = g_app.set.line_pad;
        SelectObject(dc, old);
        ReleaseDC(g_app.main, dc);
    }
    if (g_app.cell_w < 4) g_app.cell_w = 8;
    if (g_app.cell_h < 8) g_app.cell_h = 18;
}

/* ================================================================== */
/* 初始化/退出                                                          */
/* ================================================================== */
void app_init(HINSTANCE inst) {
    ZeroMemory(&g_app, sizeof(g_app));
    g_app.inst = inst;
    g_app.dpi = 96;
    settings_defaults(&g_app.set);
    settings_load(&g_app.set);
    sessions_init(&g_app.sessions);
    sessions_load(&g_app.sessions);
    g_app.theme = theme_get(g_app.set.theme >= 0 && g_app.set.theme < theme_count() ? g_app.set.theme : theme_default());
    g_app.show_sidebar = g_app.set.show_sidebar;
    g_app.broadcast = g_app.set.broadcast_default;
    char *env = NULL;
    (void)env;
}

void app_shutdown(void) {
    app_save_workspace();
    g_app.set.show_sidebar = g_app.show_sidebar;
    settings_save(&g_app.set);
    sessions_save(&g_app.sessions);
    for (int i = 0; i < g_app.tab_count; i++) {
        if (g_app.roots[i]) { app_destroy_root(g_app.roots[i]); g_app.roots[i] = NULL; }
    }
    g_app.tab_count = 0;
    sessions_free(&g_app.sessions);
    HFONT dels[] = { g_app.f_latin, g_app.f_latin_b, g_app.f_latin_i, g_app.f_latin_bi,
                     g_app.f_cjk, g_app.f_cjk_b, g_app.f_ui, g_app.f_ui_b };
    for (int i = 0; i < (int)(sizeof(dels) / sizeof(dels[0])); i++) if (dels[i]) DeleteObject(dels[i]);
}

/* ================================================================== */
/* 标签与布局                                                           */
/* ================================================================== */
void app_layout_content(void) {
    if (!g_app.main) return;
    RECT rc;
    GetClientRect(g_app.main, &rc);
    int w = rc.right, h = rc.bottom;
    int top = 0;
    if (g_app.set.show_toolbar && g_app.toolbar) {
        MoveWindow(g_app.toolbar, 0, 0, w, app_dpi(YT_TOOLBAR_H), TRUE);
        top += app_dpi(YT_TOOLBAR_H);
    } else if (g_app.toolbar) ShowWindow(g_app.toolbar, SW_HIDE);

    if (g_app.tabbar) {
        MoveWindow(g_app.tabbar, 0, top, w, app_dpi(YT_TAB_H), TRUE);
        top += app_dpi(YT_TAB_H);
    }

    int bottom = h;
    if (g_app.set.show_statusbar && g_app.statusbar) {
        bottom = h - app_dpi(YT_STATUS_H);
        MoveWindow(g_app.statusbar, 0, bottom, w, app_dpi(YT_STATUS_H), TRUE);
        ShowWindow(g_app.statusbar, SW_SHOW);
    } else if (g_app.statusbar) ShowWindow(g_app.statusbar, SW_HIDE);

    int left = 0;
    if (g_app.show_sidebar && g_app.sidebar) {
        int sw = g_app.set.sidebar_width > 0 ? app_dpi(g_app.set.sidebar_width) : app_dpi(250);
        if (sw > w / 2) sw = w / 2;
        MoveWindow(g_app.sidebar, 0, top, sw, bottom - top, TRUE);
        ShowWindow(g_app.sidebar, SW_SHOW);
        left = sw;
    } else if (g_app.sidebar) ShowWindow(g_app.sidebar, SW_HIDE);

    RECT content = { left, top, w, bottom };
    for (int i = 0; i < g_app.tab_count; i++) {
        HWND r = g_app.roots[i];
        if (!r) continue;
        if (i == g_app.active_tab) {
            MoveWindow(r, content.left, content.top, content.right - content.left, content.bottom - content.top, TRUE);
            ShowWindow(r, SW_SHOW);
            if (split_is_split(r) == 0) term_view_focus(r);
        } else {
            ShowWindow(r, SW_HIDE);
        }
    }
    if (g_app.statusbar) statusbar_update(g_app.statusbar);
}

HWND app_root_view(HWND root) {
    if (!root) return NULL;
    wchar_t cls[64] = { 0 };
    GetClassNameW(root, cls, 64);
    if (wcscmp(cls, L"YSplit") == 0) {
        HWND c = GetWindow(root, GW_CHILD);
        while (c) {
            HWND v = app_root_view(c);
            if (v) return v;
            c = GetWindow(c, GW_HWNDNEXT);
        }
        return NULL;
    }
    if (wcscmp(cls, L"YTerminalView") == 0) return root;
    return NULL;
}

static HWND app_root_view_last(HWND root) {
    if (!root) return NULL;
    if (split_is_split(root)) {
        HWND c = GetWindow(root, GW_CHILD);
        while (c) {
            HWND v = app_root_view_last(c);
            if (v) return v;
            c = GetWindow(c, GW_HWNDNEXT);
        }
        return NULL;
    }
    return root;
}

void app_root_title(int index, wchar_t *buf, int cch) {
    if (index < 0 || index >= g_app.tab_count) { buf[0] = 0; return; }
    HWND v = app_root_view(g_app.roots[index]);
    const wchar_t *t = v ? term_view_title(v) : L"新标签";
    wcsncpy(buf, t, cch - 1);
    buf[cch - 1] = 0;
}

void app_activate_tab(int index) {
    if (index < 0 || index >= g_app.tab_count) return;
    g_app.active_tab = index;
    app_layout_content();
    if (g_app.tabbar) tabbar_refresh(g_app.tabbar);
    HWND v = app_root_view_last(g_app.roots[index]);
    if (v) term_view_focus(v);
    app_status_refresh();
}

int app_find_tab(HWND root) {
    for (int i = 0; i < g_app.tab_count; i++) if (g_app.roots[i] == root) return i;
    return -1;
}

HWND app_active_root(void) {
    if (g_app.active_tab < 0 || g_app.active_tab >= g_app.tab_count) return NULL;
    return g_app.roots[g_app.active_tab];
}

HWND app_active_view(void) {
    HWND root = app_active_root();
    if (!root) return NULL;
    HWND f = GetFocus();
    HWND walk = f;
    while (walk) {
        if (walk == root) break;
        wchar_t cls[64] = { 0 };
        GetClassNameW(walk, cls, 64);
        if (wcscmp(cls, L"YTerminalView") == 0) return walk;
        walk = GetParent(walk);
    }
    return app_root_view(root);
}

void app_add_root(HWND root) {
    if (g_app.tab_count >= 128) return;
    g_app.roots[g_app.tab_count++] = root;
    g_app.active_tab = g_app.tab_count - 1;
    app_layout_content();
    if (g_app.tabbar) tabbar_refresh(g_app.tabbar);
}

void app_remove_root(HWND root) {
    int idx = app_find_tab(root);
    if (idx < 0) return;
    for (int i = idx; i < g_app.tab_count - 1; i++) g_app.roots[i] = g_app.roots[i + 1];
    g_app.tab_count--;
    if (idx < g_app.active_tab) g_app.active_tab--;
    if (g_app.active_tab >= g_app.tab_count) g_app.active_tab = g_app.tab_count - 1;
    if (g_app.active_tab < 0) g_app.active_tab = 0;
    app_layout_content();
    if (g_app.tabbar) tabbar_refresh(g_app.tabbar);
}

void app_replace_root(HWND old_root, HWND new_root) {
    int idx = app_find_tab(old_root);
    if (idx < 0) return;
    g_app.roots[idx] = new_root;
    app_layout_content();
    if (g_app.tabbar) tabbar_refresh(g_app.tabbar);
}

void app_destroy_root(HWND root) {
    if (!root) return;
    if (split_is_split(root)) split_destroy(root);
    else term_view_destroy(root);
}

void app_close_tab(int index) {
    if (index < 0 || index >= g_app.tab_count) return;
    if (g_app.tab_count == 1) {
        PostMessageW(g_app.main, WM_CLOSE, 0, 0);
        return;
    }
    HWND root = g_app.roots[index];
    app_destroy_root(root);
    app_remove_root(root);
}

void app_update_tab_title(HWND view) { if (g_app.tabbar) tabbar_refresh(g_app.tabbar); (void)view; }

void app_close_other_tabs(void) {
    if (g_app.tab_count <= 1) return;
    if (g_app.set.confirm_close && g_app.tab_count > 3) {
        wchar_t msg[160];
        _snwprintf(msg, 160, L"将关闭其他 %d 个标签，其中有正在运行的会话。\n确定继续吗？",
                   g_app.tab_count - 1);
        msg[159] = 0;
        if (MessageBoxW(g_app.main, msg, L"关闭其他标签", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    }
    int keep = g_app.active_tab;
    for (int i = g_app.tab_count - 1; i >= 0; i--) {
        if (i == keep) continue;
        app_close_tab(i);
        if (i < keep) keep--;
    }
    if (keep >= 0 && keep < g_app.tab_count) g_app.active_tab = keep;
    app_layout_content();
    if (g_app.tabbar) tabbar_refresh(g_app.tabbar);
}

void app_close_tabs_right(void) {
    int n = g_app.tab_count - g_app.active_tab - 1;
    if (n <= 0) return;
    if (g_app.set.confirm_close && n > 2) {
        wchar_t msg[160];
        _snwprintf(msg, 160, L"将关闭右侧 %d 个标签。\n确定继续吗？", n);
        msg[159] = 0;
        if (MessageBoxW(g_app.main, msg, L"关闭右侧标签", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    }
    for (int i = g_app.tab_count - 1; i > g_app.active_tab; i--) app_close_tab(i);
    app_layout_content();
    if (g_app.tabbar) tabbar_refresh(g_app.tabbar);
}
void app_refresh_tabbar(void) { if (g_app.tabbar) tabbar_refresh(g_app.tabbar); }

/* ================================================================== */
/* 打开会话                                                             */
/* ================================================================== */
HWND app_open_session(const Session *s, int activate) {
    if (!s) return NULL;
    HWND v = term_view_create(g_app.main, (Session *)s, g_app.inst);
    if (!v) return NULL;
    g_app.roots[g_app.tab_count++] = v;
    if (activate) g_app.active_tab = g_app.tab_count - 1;
    app_layout_content();
    if (g_app.tabbar) tabbar_refresh(g_app.tabbar);
    if (activate) term_view_focus(v);
    return v;
}

void app_connect_session(int index) {
    if (index < 0 || index >= g_app.sessions.n) return;
    Session *s = &g_app.sessions.items[index];
    app_open_session(s, 1);
    app_status_set((wchar_t *)L"正在连接…");
}

void app_sessions_changed(void) {
    sessions_save(&g_app.sessions);
    if (g_app.sidebar) sidebar_refresh(g_app.sidebar);
}

/* ================================================================== */
/* 剪贴板                                                               */
/* ================================================================== */
int app_clipboard_set(const char *utf8) {
    if (!OpenClipboard(g_app.main)) return 0;
    EmptyClipboard();
    size_t n = strlen(utf8);
    wchar_t *w = utf8_to_wide(utf8);
    size_t bytes = (wcslen(w) + 1) * sizeof(wchar_t);
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hg) {
        void *p = GlobalLock(hg);
        if (p) { memcpy(p, w, bytes); GlobalUnlock(hg); SetClipboardData(CF_UNICODETEXT, hg); }
        else GlobalFree(hg);
    }
    CloseClipboard();
    free(w);
    (void)n;
    return 1;
}

char *app_clipboard_get(void) {
    if (!OpenClipboard(g_app.main)) return NULL;
    char *out = NULL;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        wchar_t *w = (wchar_t *)GlobalLock(h);
        if (w) { out = wide_to_utf8(w); GlobalUnlock(h); }
    } else {
        h = GetClipboardData(CF_TEXT);
        if (h) {
            char *p = (char *)GlobalLock(h);
            if (p) { out = xstrdup(p); GlobalUnlock(h); }
        }
    }
    CloseClipboard();
    return out;
}

int app_clipboard_has_text(void) {
    int ok = IsClipboardFormatAvailable(CF_UNICODETEXT) || IsClipboardFormatAvailable(CF_TEXT);
    return ok;
}

/* ================================================================== */
/* 绘图辅助                                                             */
/* ================================================================== */
/* 接受 VT 编码色（0x01RRGGBB）或已转换的 COLORREF，统一转成 GDI 颜色 */
static COLORREF cr_in(COLORREF c) {
    u32 v = (u32)c;
    if ((v & 0xFF000000u) == 0x01000000u)
        return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    return c;
}

void app_fill(HDC hdc, RECT *rc, COLORREF color) {
    color = cr_in(color);
    COLORREF old = SetBkColor(hdc, color);
    ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, rc, L"", 0, NULL);
    SetBkColor(hdc, old);
}

void app_fill_round(HDC hdc, RECT *rc, int radius, COLORREF color) {
    color = cr_in(color);
    if (radius <= 1) { app_fill(hdc, rc, color); return; }
    HBRUSH br = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
    HPEN op = (HPEN)SelectObject(hdc, pen);
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius * 2, radius * 2);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

void app_draw_text(HDC hdc, const wchar_t *text, RECT *rc, HFONT font, COLORREF color, UINT flags) {
    color = cr_in(color);
    HFONT old = (HFONT)SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    RECT r = *rc;
    if (flags & (DT_VCENTER)) {
        /* DrawText 的 DT_VCENTER 只对单行有效 */
        DrawTextW(hdc, text, -1, &r, flags);
    } else {
        DrawTextW(hdc, text, -1, &r, flags);
    }
    SelectObject(hdc, old);
}

int app_text_width(HDC hdc, HFONT font, const wchar_t *text) {
    if (!text || !*text) return 0;
    HFONT old = (HFONT)SelectObject(hdc, font);
    SIZE sz = { 0, 0 };
    GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
    SelectObject(hdc, old);
    return sz.cx;
}

void app_draw_icon(HDC hdc, const wchar_t *glyph, RECT *rc, COLORREF color, int size_pt) {
    color = cr_in(color);
    HFONT f = mkfont(L"Segoe MDL2 Assets", size_pt, 0, 0, 0);
    HFONT old = (HFONT)SelectObject(hdc, f);
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, glyph, -1, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, old);
    DeleteObject(f);
}

/* ================================================================== */
/* 工作区                                                               */
/* ================================================================== */
void app_save_workspace(void) {
    wchar_t dir[MAX_PATH * 2], path[MAX_PATH * 2];
    yt_data_dir(dir, MAX_PATH * 2);
    _snwprintf(path, MAX_PATH * 2, L"%s\\workspace.txt", dir);
    path[MAX_PATH * 2 - 1] = 0;
    FILE *f = _wfopen(path, L"wb");
    if (!f) return;
    for (int i = 0; i < g_app.tab_count; i++) {
        Session *s = term_view_session(app_root_view(g_app.roots[i]));
        if (s && s->id[0]) fprintf(f, "%s\n", s->id);
    }
    fclose(f);
}

void app_restore_workspace(void) {
    wchar_t dir[MAX_PATH * 2], path[MAX_PATH * 2];
    yt_data_dir(dir, MAX_PATH * 2);
    _snwprintf(path, MAX_PATH * 2, L"%s\\workspace.txt", dir);
    path[MAX_PATH * 2 - 1] = 0;
    if (!g_app.set.restore_tabs) return;
    FILE *f = _wfopen(path, L"rb");
    if (!f) return;
    char line[64];
    int opened = 0;
    while (fgets(line, sizeof(line), f)) {
        yt_trim(line);
        if (!*line) continue;
        int idx = sessions_find_index(&g_app.sessions, line);
        if (idx >= 0) { app_open_session(&g_app.sessions.items[idx], opened == 0); opened++; }
        if (opened >= 8) break;
    }
    fclose(f);
}

/* ================================================================== */
/* 命令分发                                                             */
/* ================================================================== */
void app_split_active(int vertical) {
    HWND root = app_active_root();
    if (!root) return;
    HWND v1 = app_root_view(root);
    if (!v1) return;
    Session *s = term_view_session(v1);
    if (!s) return;
    Session copy = *s;
    HWND v2 = term_view_create(g_app.main, &copy, g_app.inst);
    if (!v2) return;
    HWND sp = split_create(g_app.main, root, v2, vertical);
    if (!sp) { term_view_destroy(v2); return; }
    app_replace_root(root, sp);
    term_view_focus(v2);
    app_status_set(L"已分屏");
}

void app_toggle_broadcast(void) {
    g_app.broadcast = !g_app.broadcast;
    if (g_app.toolbar) InvalidateRect(g_app.toolbar, NULL, FALSE);
    app_status_set(g_app.broadcast ? L"输入广播已开启：所有标签同步输入" : L"输入广播已关闭");
}

void app_find_in_active(void) {
    HWND v = app_active_view();
    if (v) term_view_search(v);
}

void app_dispatch_input(HWND from, const void *data, size_t len) {
    for (int i = 0; i < g_app.tab_count; i++) {
        HWND v = app_root_view(g_app.roots[i]);
        if (!v) continue;
        /* 广播时跳过来源自身，避免重复输入 */
        if (v == from) continue;
        Backend *b = term_view_backend(v);
        if (b) term_view_broadcast_text(v, (const char *)data, len);
    }
    (void)from;
}

void app_command(UINT id) {
    HWND v = app_active_view();
    switch (id) {
    case IDC_NEW_SSH: app_show_session_dialog(g_app.main, -1); break;
    case IDC_NEW_LOCAL: case IDC_NEW_TAB: app_show_session_dialog(g_app.main, -2); break;
    case IDC_NEW_SERIAL: app_show_session_dialog(g_app.main, -3); break;
    case IDC_QUICK_CONNECT: app_show_quick_connect(g_app.main); break;
    case IDC_DUPLICATE: {
        Session *s = v ? term_view_session(v) : NULL;
        if (s) app_open_session(s, 1);
        break;
    }
    case IDC_CLOSE_TAB: app_close_tab(g_app.active_tab); break;
    case IDC_COPY:
        if (v) {
            if (term_view_has_selection(v)) term_view_copy(v);
            else term_view_select_all(v), term_view_copy(v);
            app_status_set(L"已复制到剪贴板");
        }
        break;
    case IDC_PASTE: if (v) term_view_paste_clipboard(v); break;
    case IDC_COPY_ALL: if (v) { term_view_select_all(v); term_view_copy(v); } break;
    case IDC_SELECTALL: if (v) term_view_select_all(v); break;
    case IDC_CLEAR: if (v) term_view_clear(v); break;
    case IDC_FIND: app_find_in_active(); break;
    case IDC_FONT_INC:
        g_app.set.font_size++; if (g_app.set.font_size > 40) g_app.set.font_size = 40;
        app_rebuild_fonts(); app_apply_theme(); settings_save(&g_app.set); app_layout_content();
        SendMessageW(g_app.main, WM_YT_APPLY_SETTINGS, 0, 0);
        break;
    case IDC_FONT_DEC:
        g_app.set.font_size--; if (g_app.set.font_size < 6) g_app.set.font_size = 6;
        app_rebuild_fonts(); app_apply_theme(); settings_save(&g_app.set); app_layout_content();
        SendMessageW(g_app.main, WM_YT_APPLY_SETTINGS, 0, 0);
        break;
    case IDC_FONT_RESET:
        g_app.set.font_size = 11;
        app_rebuild_fonts(); app_apply_theme(); settings_save(&g_app.set); app_layout_content();
        SendMessageW(g_app.main, WM_YT_APPLY_SETTINGS, 0, 0);
        break;
    case IDC_TOGGLE_SIDEBAR:
        g_app.show_sidebar = !g_app.show_sidebar;
        app_layout_content();
        break;
    case IDC_SPLIT_V: app_split_active(1); break;
    case IDC_SPLIT_H: app_split_active(0); break;
    case IDC_BROADCAST: app_toggle_broadcast(); break;
    case IDC_LOG:
        if (v) {
            term_view_toggle_log(v);
            if (g_app.toolbar) InvalidateRect(g_app.toolbar, NULL, FALSE);
            statusbar_update(g_app.statusbar);
        }
        break;
    case IDC_RECONNECT: if (v) term_view_reconnect(v); break;
    case IDC_SFTP: app_show_sftp(g_app.main); break;
    case IDC_QUICKCMD: app_quick_commands(g_app.main); break;
    case IDC_SETTINGS: app_show_settings(g_app.main); break;
    case IDC_IMPORT: case IDC_SIDE_IMPORT: app_import_sessions(g_app.main); break;
    case IDC_ABOUT: app_show_about(g_app.main); break;
    case IDC_SIDE_NEW: app_show_session_dialog(g_app.main, -1); break;
    case IDC_EDIT_SESSION: {
        int idx = 0;
        Session *s = v ? term_view_session(v) : NULL;
        if (s) idx = sessions_find_index(&g_app.sessions, s->id);
        if (idx < 0) idx = 0;
        app_show_session_dialog(g_app.main, idx);
        break;
    }
    case IDC_RENAME_TAB: app_rename_active_tab(g_app.main); break;
    case IDC_PALETTE: app_show_palette(g_app.main); break;
    case IDC_TUNNELS: app_show_tunnels(g_app.main); break;
    case IDC_CLOSE_OTHERS: app_close_other_tabs(); break;
    case IDC_CLOSE_RIGHT: app_close_tabs_right(); break;
    case IDC_COPY_TAB_TITLE: {
        wchar_t title[192] = { 0 };
        if (g_app.tab_count > 0) app_root_title(g_app.active_tab, title, 192);
        char *u = wide_to_utf8(title);
        if (u) { app_clipboard_set(u); app_status_set(L"已复制标签标题"); free(u); }
        break;
    }
    case IDC_SHORTCUTS: app_show_shortcuts(g_app.main); break;
    case IDC_EXIT: PostMessageW(g_app.main, WM_CLOSE, 0, 0); break;
    default: break;
    }
}
