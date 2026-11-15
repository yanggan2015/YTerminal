#include "app.h"
#include "ids.h"
#include "core.h"
#include <commctrl.h>
#include <imm.h>
#include <windowsx.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* 视图结构                                                             */
/* ------------------------------------------------------------------ */
typedef struct TermView {
    HWND hwnd;
    Session s;
    VT *vt;
    Backend *back;
    int cols, rows;
    int pad;
    int cell_w, cell_h, ascent;
    HDC memdc;
    HBITMAP bmp;
    int bmp_w, bmp_h;
    int blink_on;
    int focused;

    int sel_active;
    int sel_line0, sel_col0, sel_line1, sel_col1;
    int mouse_down;
    int dragging;
    int dbl_click;

    HWND search;
    int search_visible;
    int match_line, match_col, match_cells;
    int search_start_line;

    int scroll_drag;
    int scroll_hover;

    wchar_t title[192];
    int title_custom;

    HANDLE log;
    int log_on;
    char log_pending[2048];
    int log_plen;

    char trig_buf[1024];
    DWORD trig_last[8];

    /* 多字节编码分包残留 */
    u8  enc_pend[4];
    int enc_pend_len;

    int closing;
    int alt_pending;
    int suppress_char;
    WPARAM alt_vk;

    int hidpi;
    WPARAM last_char;
} TermView;

static const wchar_t *TERM_CLASS = L"YTerminalView";
static int g_registered = 0;
static WNDPROC g_search_old_proc = NULL;

#define TIMER_BLINK 1
#define TIMER_SCROLL_TICK 2

static TermView *tv_from(HWND h) { return (TermView *)GetWindowLongPtrW(h, GWLP_USERDATA); }
static int y_cursor_bottom(TermView *tv, int cy);
static void tv_sel_norm(TermView *tv);

/* ------------------------------------------------------------------ */
/* 颜色工具                                                             */
/* ------------------------------------------------------------------ */
static COLORREF tv_fg(TermView *tv, u32 c, u8 attr) {
    Theme *t = g_app.theme;
    COLORREF col;
    if (VT_IS_DEFAULT(c)) col = t->fg;
    else if (VT_IS_RGB(c)) col = RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    else {
        int idx = (int)(c & 0xFF);
        if ((attr & ATTR_BOLD) && idx < 8) idx += 8;
        col = app_cr((u32)idx, t->fg);
    }
    if (attr & ATTR_DIM) {
        COLORREF bg = tv_fg(tv, VT_DEFAULT_BG, 0);
        int r = (GetRValue(col) * 55 + GetRValue(bg) * 45) / 100;
        int g = (GetGValue(col) * 55 + GetGValue(bg) * 45) / 100;
        int b = (GetBValue(col) * 55 + GetBValue(bg) * 45) / 100;
        col = RGB(r, g, b);
    }
    return col;
}
static COLORREF tv_bg(TermView *tv, u32 c) {
    if (VT_IS_DEFAULT(c)) return g_app.theme->bg;
    if (VT_IS_RGB(c)) return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    return app_cr(c, g_app.theme->bg);
}

/* ------------------------------------------------------------------ */
/* 字体                                                                 */
/* ------------------------------------------------------------------ */
static HFONT tv_font(TermView *tv, u8 attr, int cjk) {
    int b = (attr & ATTR_BOLD) != 0;
    int i = (attr & ATTR_ITALIC) != 0;
    if (cjk) return b ? g_app.f_cjk_b : g_app.f_cjk;
    if (b && i) return g_app.f_latin_bi;
    if (b) return g_app.f_latin_b;
    if (i) return g_app.f_latin_i;
    return g_app.f_latin;
}

/* ------------------------------------------------------------------ */
/* 日志                                                                 */
/* ------------------------------------------------------------------ */
static void tv_log_flush_line(TermView *tv, const char *line) {
    if (!tv->log || !tv->log_on) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char head[64];
    _snprintf(head, sizeof(head), "[%04d-%02d-%02d %02d:%02d:%02d] ",
              (int)st.wYear, (int)st.wMonth, (int)st.wDay, (int)st.wHour, (int)st.wMinute, (int)st.wSecond);
    DWORD wrote = 0;
    WriteFile(tv->log, head, (DWORD)strlen(head), &wrote, NULL);
    if (line && *line) WriteFile(tv->log, line, (DWORD)strlen(line), &wrote, NULL);
    const char *nl = "\r\n";
    WriteFile(tv->log, nl, 2, &wrote, NULL);
}

static void tv_log_start(TermView *tv) {
    if (tv->log_on) return;
    wchar_t dir[MAX_PATH], path[MAX_PATH], name[128], stamp[32];
    wcsncpy(dir, g_app.set.log_dir, MAX_PATH - 1); dir[MAX_PATH - 1] = 0;
    yt_mkdir(dir);
    yt_now_stamp(stamp, 32);
    wchar_t *sn = utf8_to_wide(tv->s.name);
    _snwprintf(name, 128, L"%s", sn);
    free(sn);
    for (wchar_t *p = name; *p; p++) if (*p == L'\\' || *p == L'/' || *p == L':' || *p == L'*' || *p == L'?' || *p == L' ' || *p == L'|' || *p == L'"') *p = L'_';
    _snwprintf(path, MAX_PATH, L"%s\\%s_%s.log", dir, name, stamp);
    path[MAX_PATH - 1] = 0;
    tv->log = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (tv->log == INVALID_HANDLE_VALUE) { tv->log = NULL; return; }
    tv->log_on = 1;
    tv->log_plen = 0;
    tv_log_flush_line(tv, "=== YTerminal 会话日志开始 ===");
    char sub[160];
    session_subtitle(&tv->s, sub, 160);
    tv_log_flush_line(tv, sub);
}

static void tv_log_stop(TermView *tv) {
    if (!tv->log_on) return;
    if (tv->log_plen) { tv->log_pending[tv->log_plen] = 0; tv_log_flush_line(tv, tv->log_pending); tv->log_plen = 0; }
    tv_log_flush_line(tv, "=== YTerminal 会话日志结束 ===");
    if (tv->log) CloseHandle(tv->log);
    tv->log = NULL;
    tv->log_on = 0;
}

/* ------------------------------------------------------------------ */
/* 触发器                                                               */
/* ------------------------------------------------------------------ */
static void tv_triggers(TermView *tv, const char *data, size_t n) {
    if (!tv->s.triggers[0]) return;
    size_t cap = sizeof(tv->trig_buf) - 1;
    if (n >= cap) data += n - cap, n = cap;
    size_t old = strlen(tv->trig_buf);
    if (old + n > cap) {
        size_t drop = old + n - cap;
        memmove(tv->trig_buf, tv->trig_buf + drop, old - drop);
        old -= drop;
        tv->trig_buf[old] = 0;
    }
    memcpy(tv->trig_buf + old, data, n);
    tv->trig_buf[old + n] = 0;

    char copy[1024];
    strncpy(copy, tv->s.triggers, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = 0;
    char *save = NULL;
    int idx = 0;
    for (char *line = strtok_s(copy, "\r\n", &save); line && idx < 8; line = strtok_s(NULL, "\r\n", &save), idx++) {
        char *arrow = strstr(line, "=>");
        if (!arrow) continue;
        *arrow = 0;
        char *pat = line;
        char *send = arrow + 2;
        while (*pat == ' ') pat++;
        yt_trim(pat);
        yt_trim(send);
        if (!*pat || !*send) continue;
        DWORD now = GetTickCount();
        if (tv->trig_last[idx] && now - tv->trig_last[idx] < 3000) continue;
        if (yt_wildcard_match(pat, tv->trig_buf)) {
            tv->trig_last[idx] = now;
            char buf[512];
            _snprintf(buf, sizeof(buf), "%s\r", send);
            backend_write(tv->back, buf, strlen(buf));
        }
    }
}

/* ------------------------------------------------------------------ */
/* VT 回调                                                              */
/* ------------------------------------------------------------------ */
static void cb_write(void *user, const char *s, size_t n) {
    TermView *tv = (TermView *)user;
    if (tv->back) backend_write(tv->back, s, n);
}
static void cb_bell(void *user) {
    TermView *tv = (TermView *)user;
    if (g_app.set.bell) MessageBeep(MB_OK);
    tv->blink_on = 1;
}
static void cb_title(void *user, const char *title) {
    TermView *tv = (TermView *)user;
    wchar_t *w = utf8_to_wide(title);
    wcsncpy(tv->title, w, 191);
    tv->title[191] = 0;
    free(w);
    tv->title_custom = 1;
    PostMessageW(g_app.main, WM_YT_TITLE_CHANGED, (WPARAM)tv->hwnd, 0);
}
static void cb_notify(void *user, const char *title, const char *body) {
    (void)user;
    wchar_t *w1 = utf8_to_wide(title);
    wchar_t *w2 = utf8_to_wide(body);
    wchar_t text[512];
    _snwprintf(text, 512, L"%s\n%s", w1, w2);
    text[511] = 0;
    free(w1); free(w2);
    (void)text;
    MessageBeep(MB_ICONINFORMATION);
}
static void cb_clip(void *user, const char *text) {
    (void)user;
    app_clipboard_set(text);
}

/* ------------------------------------------------------------------ */
/* 网格与滚动                                                           */
/* ------------------------------------------------------------------ */
static void tv_recalc(TermView *tv, int w, int h) {
    int sbw = app_dpi(10);
    int cols = (w - tv->pad * 2 - sbw) / (tv->cell_w > 0 ? tv->cell_w : 8);
    int rows = (h - tv->pad * 2) / (tv->cell_h > 0 ? tv->cell_h : 16);
    if (cols < 2) cols = 2;
    if (rows < 2) rows = 2;
    if (tv->cols != cols || tv->rows != rows) {
        tv->cols = cols;
        tv->rows = rows;
        vt_resize(tv->vt, cols, rows);
        if (tv->back) backend_resize(tv->back, cols, rows);
    }
}

static void tv_scroll_to_line(TermView *tv, int line) {
    int hist = vt_history_len(tv->vt);
    int rows = vt_rows(tv->vt);
    int off = vt_view_offset(tv->vt);
    int row = line - (hist - off);
    if (row < 0) vt_scroll(tv->vt, row);
    else if (row >= rows) vt_scroll(tv->vt, row - rows + 1);
    InvalidateRect(tv->hwnd, NULL, FALSE);
}

static void tv_copy_selection(TermView *tv) {
    if (!tv->sel_active) return;
    char *txt = vt_selection_text(tv->vt, tv->sel_line0, tv->sel_col0, tv->sel_line1, tv->sel_col1);
    if (txt && *txt) app_clipboard_set(txt);
    free(txt);
}

static void tv_sel_norm(TermView *tv) {
    if (tv->sel_line0 > tv->sel_line1 ||
        (tv->sel_line0 == tv->sel_line1 && tv->sel_col0 > tv->sel_col1)) {
        int l = tv->sel_line0, c = tv->sel_col0;
        tv->sel_line0 = tv->sel_line1; tv->sel_col0 = tv->sel_col1;
        tv->sel_line1 = l; tv->sel_col1 = c;
    }
}

/* 返回 1 表示该行属于选区，并给出列范围 */
static int tv_sel_span(TermView *tv, int line, int cols, int *sc0, int *sc1) {
    if (!tv->sel_active) return 0;
    if (line < tv->sel_line0 || line > tv->sel_line1) return 0;
    *sc0 = (line == tv->sel_line0) ? tv->sel_col0 : 0;
    *sc1 = (line == tv->sel_line1) ? tv->sel_col1 : cols - 1;
    if (*sc1 >= cols) *sc1 = cols - 1;
    return 1;
}

/* ------------------------------------------------------------------ */
/* 绘制                                                                 */
/* ------------------------------------------------------------------ */
static void tv_paint_cells(TermView *tv, HDC hdc, int view_w, int view_h) {
    Theme *t = g_app.theme;
    RECT rc = { 0, 0, view_w, view_h };
    app_fill(hdc, &rc, t->bg);

    int hist = vt_history_len(tv->vt);
    int off = vt_view_offset(tv->vt);
    int rows = tv->rows, cols = tv->cols;

    SetBkMode(hdc, TRANSPARENT);
    SetTextAlign(hdc, TA_LEFT | TA_TOP);

    int match_row = (tv->match_cells > 0) ? (tv->match_line - (hist - off)) : -999;

    for (int r = 0; r < rows; r++) {
        Cell *line = vt_line(tv->vt, vt_line_index_for_row(tv->vt, r));
        if (!line) continue;
        int y = tv->pad + r * tv->cell_h;

        /* 背景 */
        COLORREF curbg = CLR_INVALID;
        int run_x = 0;
        for (int x = 0; x < cols; x++) {
            Cell *c = &line[x];
            if (c->width == 0) continue;
            COLORREF bg = tv_bg(tv, c->bg);
            if (c->attr & ATTR_INVERSE) bg = tv_fg(tv, c->fg, 0);
            if (c->attr & ATTR_HIDDEN) bg = bg;
            int in_sel = 0;
            int l = vt_line_index_for_row(tv->vt, r);
            int c0 = 0, c1 = -1;
            if (tv_sel_span(tv, l, cols, &c0, &c1) && x >= c0 && x <= c1) in_sel = 1;
            if (in_sel) bg = t->selection;
            if (r == match_row && x >= tv->match_col && x < tv->match_col + tv->match_cells) bg = RGB(255, 196, 64);

            if (curbg == CLR_INVALID) { curbg = bg; run_x = x; }
            else if (bg != curbg) {
                int wcells = x - run_x;
                if (curbg != t->bg) {
                    RECT br = { tv->pad + run_x * tv->cell_w, y, tv->pad + (run_x + wcells) * tv->cell_w, y + tv->cell_h };
                    app_fill(hdc, &br, curbg);
                }
                curbg = bg; run_x = x;
            }
        }
        if (curbg != CLR_INVALID && curbg != t->bg) {
            RECT br = { tv->pad + run_x * tv->cell_w, y, tv->pad + cols * tv->cell_w, y + tv->cell_h };
            app_fill(hdc, &br, curbg);
        }

        /* 文本 */
        int x = 0;
        while (x < cols) {
            Cell *c = &line[x];
            if (c->width == 0) { x++; continue; }
            int is_space = (c->cp == ' ' || c->cp == 0) && c->cp2 == 0;
            if (is_space) { x += (c->width == 2 ? 2 : 1); continue; }

            if (c->width == 2) {
                COLORREF fg = tv_fg(tv, c->fg, c->attr);
                COLORREF bg = tv_bg(tv, c->bg);
                if (c->attr & ATTR_INVERSE) { COLORREF tmp = fg; fg = bg; bg = tmp; }
                int l = vt_line_index_for_row(tv->vt, r);
                int c0 = 0, c1 = -1;
                if (tv_sel_span(tv, l, cols, &c0, &c1) && x >= c0 && x <= c1) fg = t->selection_text;
                if (r == match_row && x >= tv->match_col && x < tv->match_col + tv->match_cells) fg = RGB(24, 24, 24);
                if (c->attr & ATTR_HIDDEN) fg = bg;
                wchar_t wbuf[2] = { (wchar_t)c->cp, 0 };
                HFONT f = tv_font(tv, c->attr, 1);
                HFONT old = (HFONT)SelectObject(hdc, f);
                SetTextColor(hdc, fg);
                ExtTextOutW(hdc, tv->pad + x * tv->cell_w, y + (tv->cell_h - g_app.font_h) / 2, 0, NULL, wbuf, 1, NULL);
                SelectObject(hdc, old);
                x += 2;
                continue;
            }

            /* 收集同属性窄字符 */
            int start = x;
            wchar_t wbuf[256];
            int used = 0;
            u8 attr0 = c->attr;
            u32 fg0 = c->fg, bg0 = c->bg;
            while (x < cols && used < 240) {
                Cell *cc = &line[x];
                if (cc->width != 1 || cc->attr != attr0 || cc->fg != fg0 || cc->bg != bg0) break;
                if ((cc->cp == ' ' || cc->cp == 0) && cc->cp2 == 0) { wbuf[used++] = L' '; x++; continue; }
                if (ucs_width(cc->cp) == 2) break;
                wbuf[used++] = (wchar_t)(cc->cp ? cc->cp : ' ');
                if (cc->cp2) wbuf[used++] = (wchar_t)cc->cp2;
                x++;
            }
            if (!used) { x++; continue; }

            COLORREF fg = tv_fg(tv, fg0, attr0);
            COLORREF bg = tv_bg(tv, bg0);
            if (attr0 & ATTR_INVERSE) { COLORREF tmp = fg; fg = bg; bg = tmp; }
            int l = vt_line_index_for_row(tv->vt, r);
            int c0 = 0, c1 = -1;
            tv_sel_span(tv, l, cols, &c0, &c1);
            int sel_from = c0 < start ? start : c0;
            int sel_to = c1 > (start + used - 1) ? (start + used - 1) : c1;
            if (sel_to >= sel_from) fg = t->selection_text;
            if (attr0 & ATTR_HIDDEN) fg = bg;
            HFONT f = tv_font(tv, attr0, 0);
            HFONT old = (HFONT)SelectObject(hdc, f);
            SetTextColor(hdc, fg);
            ExtTextOutW(hdc, tv->pad + start * tv->cell_w, y + (tv->cell_h - g_app.font_h) / 2, 0, NULL, wbuf, (UINT)used, NULL);

            if (attr0 & ATTR_UNDERLINE) {
                RECT ur = { tv->pad + start * tv->cell_w, y + tv->cell_h - app_dpi(2), tv->pad + (start + used) * tv->cell_w, y + tv->cell_h };
                app_fill(hdc, &ur, fg);
            }
            if (attr0 & ATTR_STRIKE) {
                RECT sr = { tv->pad + start * tv->cell_w, y + tv->cell_h / 2, tv->pad + (start + used) * tv->cell_w, y + tv->cell_h / 2 + 1 };
                app_fill(hdc, &sr, fg);
            }
            SelectObject(hdc, old);
        }
    }

    /* 光标 */
    int show_cursor = tv->focused && vt_cursor_visible(tv->vt) && off == 0 && !vt_is_alt(tv->vt);
    if (vt_is_alt(tv->vt)) show_cursor = tv->focused && vt_cursor_visible(tv->vt) && off == 0;
    if (show_cursor && (!g_app.set.cursor_blink || tv->blink_on)) {
        int cx = vt_cursor_x(tv->vt), cy = vt_cursor_y(tv->vt);
        int style = g_app.set.cursor_style;
        if (style == 0) style = vt_cursor_style(tv->vt) == 1 ? 2 : (vt_cursor_style(tv->vt) == 2 ? 3 : 1);
        RECT cr;
        if (style == 2) {
            cr.left = tv->pad + cx * tv->cell_w;
            cr.right = cr.left + tv->cell_w;
            cr.top = y_cursor_bottom(tv, cy), cr.bottom = cr.top + app_dpi(2);
        } else if (style == 3) {
            cr.left = tv->pad + cx * tv->cell_w;
            cr.right = cr.left + app_dpi(2);
            cr.top = tv->pad + cy * tv->cell_h;
            cr.bottom = cr.top + tv->cell_h;
        } else {
            cr.left = tv->pad + cx * tv->cell_w;
            cr.right = cr.left + tv->cell_w;
            cr.top = tv->pad + cy * tv->cell_h;
            cr.bottom = cr.top + tv->cell_h;
            app_fill(hdc, &cr, t->cursor);
            Cell *cc = NULL;
            Cell *line = vt_line(tv->vt, vt_line_index_for_row(tv->vt, cy));
            if (line && cx < tv->cols) cc = &line[cx];
            if (cc && cc->cp && cc->cp != ' ') {
                COLORREF ct = t->cursor_text;
                wchar_t wbuf[2] = { (wchar_t)cc->cp, 0 };
                HFONT f = tv_font(tv, 0, ucs_width(cc->cp) == 2);
                HFONT old = (HFONT)SelectObject(hdc, f);
                SetTextColor(hdc, ct);
                ExtTextOutW(hdc, cr.left, cr.top + (tv->cell_h - tv->ascent) / 2, 0, NULL, wbuf, 1, NULL);
                SelectObject(hdc, old);
            }
            goto cursor_done;
        }
        app_fill(hdc, &cr, t->cursor);
    cursor_done:;
    }

    /* 状态提示 */
    if (tv->back) {
        int st = backend_state(tv->back);
        if (st != BS_CONNECTED) {
            const char *msg = backend_status(tv->back);
            wchar_t *w = utf8_to_wide(msg);
            int tw = app_text_width(hdc, g_app.f_ui, w);
            int bw = tw + app_dpi(20);
            int bh = app_dpi(24);
            RECT br = { view_w - bw - app_dpi(14), view_h - bh - app_dpi(10), view_w - app_dpi(14), view_h - app_dpi(10) };
            app_fill_round(hdc, &br, app_dpi(4), st == BS_ERROR ? RGB(120, 40, 40) : g_app.theme->ui_panel);
            br.left += app_dpi(10);
            app_draw_text(hdc, w, &br, g_app.f_ui, st == BS_ERROR ? RGB(255, 220, 220) : g_app.theme->ui_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            free(w);
        }
    }
}

/* 光标 y 辅助 */
static int y_cursor_bottom(TermView *tv, int cy) { return tv->pad + (cy + 1) * tv->cell_h - app_dpi(3); }

static void tv_paint_scrollbar(TermView *tv, HDC hdc, int w, int h) {
    int total = vt_total_lines(tv->vt);
    if (total <= tv->rows) return;
    int sbw = app_dpi(10);
    int x = w - sbw - app_dpi(2);
    RECT track = { x, tv->pad, x + app_dpi(6), h - tv->pad };
    app_fill_round(hdc, &track, app_dpi(3), g_app.theme->ui_panel);

    int max_off = vt_history_len(tv->vt);
    int off = vt_view_offset(tv->vt);
    int thumb_h = (int)((float)track.bottom * tv->rows / total);
    if (thumb_h < app_dpi(30)) thumb_h = app_dpi(30);
    int travel = (track.bottom - track.top) - thumb_h;
    int pos = max_off > 0 ? (int)((float)travel * (max_off - off) / max_off) : travel;
    RECT th = { track.left, track.top + pos, track.right, track.top + pos + thumb_h };
    app_fill_round(hdc, &th, app_dpi(3), g_app.theme->ui_dim);
}

/* 关键字高亮：在已绘制文本上按规则叠色（大小写不敏感） */
static void tv_paint_highlights(TermView *tv, HDC hdc) {
    if (!g_app.set.highlights[0]) return;
    YtRule rules[12];
    int nr = yt_rules_parse(g_app.set.highlights, rules, 12);
    if (nr <= 0) return;
    SetBkMode(hdc, TRANSPARENT);
    for (int r = 0; r < tv->rows; r++) {
        Cell *line = vt_line(tv->vt, vt_line_index_for_row(tv->vt, r));
        if (!line) continue;
        wchar_t text[600];
        int colmap[600];
        int m = 0;
        for (int x = 0; x < tv->cols && m < 590; x++) {
            Cell *c = &line[x];
            if (c->width == 0) continue;
            text[m] = (wchar_t)(c->cp ? c->cp : ' ');
            colmap[m] = x;
            m++;
        }
        for (int k = 0; k < nr; k++) {
            if (!rules[k].text[0]) continue;
            wchar_t *kw = utf8_to_wide(rules[k].text);
            if (!kw) continue;
            int kl = (int)wcslen(kw);
            if (kl > 0 && kl <= m) {
                for (int i = 0; i + kl <= m; i++) {
                    int hit = 1;
                    for (int j = 0; j < kl; j++) {
                        if (towlower((wint_t)text[i + j]) != towlower((wint_t)kw[j])) { hit = 0; break; }
                    }
                    if (!hit) continue;
                    COLORREF col = app_cr(rules[k].color, g_app.theme->fg);
                    for (int j = 0; j < kl; j++) {
                        int x = colmap[i + j];
                        Cell *c = &line[x];
                        if (c->width == 0) continue;
                        if ((c->cp == ' ' || c->cp == 0) && c->cp2 == 0) continue;
                        HFONT old = (HFONT)SelectObject(hdc, tv_font(tv, c->attr, 0));
                        SetTextColor(hdc, col);
                        wchar_t wbuf[2] = { text[i + j], 0 };
                        ExtTextOutW(hdc, tv->pad + x * tv->cell_w,
                                    tv->pad + r * tv->cell_h + (tv->cell_h - g_app.font_h) / 2,
                                    0, NULL, wbuf, 1, NULL);
                        SelectObject(hdc, old);
                    }
                    i += kl - 1;
                }
            }
            free(kw);
        }
    }
}
static void tv_paint(TermView *tv) {
    RECT rc;
    GetClientRect(tv->hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) return;

    HDC hdc = GetDC(tv->hwnd);
    if (!tv->memdc) tv->memdc = CreateCompatibleDC(hdc);
    if (!tv->bmp || tv->bmp_w != w || tv->bmp_h != h) {
        if (tv->bmp) DeleteObject(tv->bmp);
        tv->bmp = CreateCompatibleBitmap(hdc, w, h);
        tv->bmp_w = w; tv->bmp_h = h;
        SelectObject(tv->memdc, tv->bmp);
    }
    tv_paint_cells(tv, tv->memdc, w, h);
    tv_paint_highlights(tv, tv->memdc);
    tv_paint_scrollbar(tv, tv->memdc, w, h);

    /* 搜索框背景不在此绘制，由 EDIT 自身负责 */
    BitBlt(hdc, 0, 0, w, h, tv->memdc, 0, 0, SRCCOPY);
    ReleaseDC(tv->hwnd, hdc);
}

/* ------------------------------------------------------------------ */
/* 输入                                                                 */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* 编码转换：出站 UTF-8 -> 会话编码，入站 会话编码 -> UTF-8              */
/* ------------------------------------------------------------------ */
static void tv_write_utf8(TermView *tv, const char *data, size_t len) {
    if (!tv || !tv->back || !len) return;
    if (tv->s.encoding == YT_ENC_UTF8) { backend_write(tv->back, data, len); return; }
    size_t n = 0;
    char *out = yt_utf8_to_bytes(data, len, tv->s.encoding, &n);
    if (out) {
        if (n) backend_write(tv->back, out, n);
        free(out);
    }
}

static size_t utf8_complete_len(const u8 *b, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned char c = b[i];
        size_t need;
        if (c < 0x80) need = 1;
        else if ((c & 0xE0) == 0xC0) need = 2;
        else if ((c & 0xF0) == 0xE0) need = 3;
        else if ((c & 0xF8) == 0xF0) need = 4;
        else { i++; continue; }
        if (i + need > n) return i;
        i += need;
    }
    return n;
}

/* 入站解码：转成 UTF-8，并把跨包的半个字符留到下一包 */
static char *tv_decode_input(TermView *tv, const u8 *data, size_t n, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!data || !n) return NULL;
    int enc = tv->s.encoding;
    size_t total = (size_t)tv->enc_pend_len + n;
    u8 *buf = (u8 *)xmalloc(total + 1);
    memcpy(buf, tv->enc_pend, (size_t)tv->enc_pend_len);
    memcpy(buf + tv->enc_pend_len, data, n);

    size_t keep;
    if (enc == YT_ENC_UTF8) {
        keep = utf8_complete_len(buf, total);
    } else {
        size_t i = 0;
        while (i < total) {
            if (yt_enc_is_lead(enc, buf[i])) {
                if (i + 1 >= total) break;
                i += 2;
            } else i++;
        }
        keep = i;
    }
    if (keep < total) {
        tv->enc_pend_len = (int)(total - keep);
        memcpy(tv->enc_pend, buf + keep, (size_t)tv->enc_pend_len);
    } else {
        tv->enc_pend_len = 0;
    }

    char *out;
    if (enc == YT_ENC_UTF8) {
        out = (char *)xmalloc(keep + 1);
        memcpy(out, buf, keep);
        out[keep] = 0;
        if (out_len) *out_len = keep;
    } else {
        out = yt_bytes_to_utf8((const char *)buf, keep, enc, out_len);
    }
    free(buf);
    return out;
}

static void tv_send(TermView *tv, const void *data, size_t len) {
    if (!len) return;
    vt_scroll_to_bottom(tv->vt);
    if (g_app.broadcast) app_dispatch_input(tv->hwnd, data, len);
    else tv_write_utf8(tv, (const char *)data, len);
    tv->blink_on = 1;
    InvalidateRect(tv->hwnd, NULL, FALSE);
}

static size_t mod_seq(char *out, const char *base, int mod) {
    /* base 形如 "\x1b[A"，mod 1=Shift 2=Alt 3=Shift+Alt 5=Ctrl ... */
    if (!mod) { strcpy(out, base); return strlen(base); }
    if (base[0] == '\x1b' && base[1] == '[') {
        const char *fin = base + 2;
        int m = mod;
        if (m == 1) m = 2; else if (m == 2) m = 3; else if (m == 3) m = 4;
        else if (m == 5) m = 6;
        _snprintf(out, 16, "\x1b[1;%d%c", m, fin[0]);
        return strlen(out);
    }
    strcpy(out, base);
    return strlen(base);
}

static size_t key_seq(TermView *tv, WPARAM vk, int ctrl, int alt, int shift, char *out) {
    int appcur = vt_app_cursor(tv->vt);
    int mod = (shift ? 1 : 0) | (alt ? 2 : 0) | (ctrl ? 4 : 0);
    switch (vk) {
    case VK_RETURN: strcpy(out, "\r"); return 1;
    case VK_BACK: strcpy(out, "\x7f"); return 1;
    case VK_TAB:
        if (shift) { strcpy(out, "\x1b[Z"); return 3; }
        strcpy(out, "\t"); return 1;
    case VK_ESCAPE: strcpy(out, "\x1b"); return 1;
    case VK_UP: return mod_seq(out, appcur ? "\x1bOA" : "\x1b[A", mod);
    case VK_DOWN: return mod_seq(out, appcur ? "\x1bOB" : "\x1b[B", mod);
    case VK_RIGHT: return mod_seq(out, appcur ? "\x1bOC" : "\x1b[C", mod);
    case VK_LEFT: return mod_seq(out, appcur ? "\x1bOD" : "\x1b[D", mod);
    case VK_HOME: return mod_seq(out, "\x1b[H", mod);
    case VK_END: return mod_seq(out, "\x1b[F", mod);
    case VK_INSERT: return mod_seq(out, "\x1b[2~", mod);
    case VK_DELETE: return mod_seq(out, "\x1b[3~", mod);
    case VK_PRIOR: return mod_seq(out, "\x1b[5~", mod);
    case VK_NEXT: return mod_seq(out, "\x1b[6~", mod);
    case VK_F1: return mod_seq(out, appcur ? "\x1bOP" : "\x1b[11~", mod);
    case VK_F2: return mod_seq(out, appcur ? "\x1bOQ" : "\x1b[12~", mod);
    case VK_F3: return mod_seq(out, appcur ? "\x1bOR" : "\x1b[13~", mod);
    case VK_F4: return mod_seq(out, appcur ? "\x1bOS" : "\x1b[14~", mod);
    case VK_F5: return mod_seq(out, "\x1b[15~", mod);
    case VK_F6: return mod_seq(out, "\x1b[17~", mod);
    case VK_F7: return mod_seq(out, "\x1b[18~", mod);
    case VK_F8: return mod_seq(out, "\x1b[19~", mod);
    case VK_F9: return mod_seq(out, "\x1b[20~", mod);
    case VK_F10: return mod_seq(out, "\x1b[21~", mod);
    case VK_F11: return mod_seq(out, "\x1b[23~", mod);
    case VK_F12: return mod_seq(out, "\x1b[24~", mod);
    default: break;
    }
    return 0;
}

static void tv_show_search(TermView *tv, int show) {
    tv->search_visible = show;
    ShowWindow(tv->search, show ? SW_SHOW : SW_HIDE);
    if (show) {
        RECT rc; GetClientRect(tv->hwnd, &rc);
        int sw = app_dpi(300), sh = app_dpi(28);
        SetWindowPos(tv->search, HWND_TOP, rc.right - sw - app_dpi(16), app_dpi(6), sw, sh, SWP_SHOWWINDOW);
        SetFocus(tv->search);
        SendMessageW(tv->search, EM_SETSEL, 0, -1);
    } else {
        tv->match_cells = 0;
        SetFocus(tv->hwnd);
        InvalidateRect(tv->hwnd, NULL, FALSE);
    }
}

static void tv_find_next(TermView *tv, int backward) {
    wchar_t wt[256];
    GetWindowTextW(tv->search, wt, 256);
    char *needle = wide_to_utf8(wt);
    if (!needle || !*needle) { free(needle); return; }
    int total = vt_total_lines(tv->vt);
    int start = tv->match_line;
    if (!start) start = backward ? total : 0;
    int line = -1, col = 0;
    int found = 0;
    if (backward) {
        for (int s = start - 1; s >= 0 && !found; s--) {
            if (vt_find(tv->vt, needle, s, &line, &col)) {
                if (line <= s) found = 1;
            }
        }
        if (!found) {
            for (int s = total - 1; s >= 0 && !found; s--) {
                if (vt_find(tv->vt, needle, s, &line, &col)) found = 1;
            }
        }
    } else {
        found = vt_find(tv->vt, needle, start + 1, &line, &col);
        if (!found) found = vt_find(tv->vt, needle, 0, &line, &col);
    }
    if (found) {
        tv->match_line = line;
        tv->match_col = col;
        int cells = 0;
        for (const char *p = needle; *p;) {
            u32 cp = 0;
            int n = utf8_decode_one((const u8 *)p, strlen(p), &cp);
            if (n <= 0) { p++; continue; }
            int w = ucs_width(cp);
            cells += w > 0 ? w : 1;
            p += n;
        }
        tv->match_cells = cells > 0 ? cells : 1;
        tv_scroll_to_line(tv, line);
        InvalidateRect(tv->hwnd, NULL, FALSE);
    } else {
        MessageBeep(MB_ICONASTERISK);
    }
    free(needle);
}

static void tv_context_menu(TermView *tv) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDC_COPY, L"复制 (Ctrl+Shift+C)");
    AppendMenuW(m, MF_STRING, IDC_PASTE, L"粘贴 (Ctrl+Shift+V)");
    AppendMenuW(m, MF_STRING, IDC_SELECTALL, L"全选");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDC_FIND, L"查找… (Ctrl+F)");
    AppendMenuW(m, MF_STRING, IDC_CLEAR, L"清屏");
    AppendMenuW(m, MF_STRING, IDC_LOG, tv->log_on ? L"停止会话日志" : L"开始会话日志");
    AppendMenuW(m, MF_STRING, IDC_QUICKCMD, L"快捷命令…");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDC_SPLIT_V, L"右侧分屏");
    AppendMenuW(m, MF_STRING, IDC_SPLIT_H, L"下方分屏");
    AppendMenuW(m, MF_STRING, IDC_SFTP, L"SFTP 文件传输");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDC_FONT_INC, L"放大字体");
    AppendMenuW(m, MF_STRING, IDC_FONT_DEC, L"缩小字体");
    AppendMenuW(m, MF_STRING, IDC_CLOSE_TAB, L"关闭标签");
    POINT pt;
    GetCursorPos(&pt);
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, g_app.main, NULL);
    DestroyMenu(m);
    if (cmd) PostMessageW(g_app.main, WM_COMMAND, (WPARAM)cmd, 0);
}

static void tv_mouse_report(TermView *tv, int button, int x, int y, int release, int wheel) {
    if (!tv->back || vt_mouse_mode(tv->vt) == 0) return;
    int col = x / tv->cell_w + 1;
    int row = y / tv->cell_h + 1;
    if (col < 1) col = 1;
    if (row < 1) row = 1;
    char buf[64];
    int mode = vt_mouse_mode(tv->vt);
    if (vt_mouse_sgr(tv->vt)) {
        int b = button;
        if (wheel) b = button;
        _snprintf(buf, sizeof(buf), "\x1b[<%d;%d;%d%c", b, col, row, release ? 'm' : 'M');
    } else {
        int b = button + 32;
        if (release) b = 3 + 32;
        if (wheel) b = button + 32;
        _snprintf(buf, sizeof(buf), "\x1b[M%c%c%c", b, col + 32, row + 32);
    }
    backend_write(tv->back, buf, strlen(buf));
    (void)mode;
}

/* ------------------------------------------------------------------ */
/* 链接识别与打开（Ctrl+点击）                                           */
/* ------------------------------------------------------------------ */
static int link_sep(char c) {
    return c == ' ' || c == '\t' || c == '"' || c == '\'' || c == '<' || c == '>' ||
           c == '`' || c == '|' || c == 0;
}

static char *tv_line_with_map(TermView *tv, int line, int *map) {
    Cell *cells = vt_line(tv->vt, line);
    if (!cells) return NULL;
    char *txt = (char *)xmalloc((size_t)tv->cols * 8 + 2);
    int bytes = 0;
    for (int c = 0; c < tv->cols; c++) {
        if (map) map[c] = bytes;
        if (cells[c].width == 0) continue;   /* 双宽字符的占位格 */
        u32 cp = cells[c].cp;
        if (cp == 0 || cp == 0xFFFFFFFFu) { txt[bytes++] = ' '; continue; }
        char tmp[8];
        size_t n = utf8_encode_one(cp, tmp);
        memcpy(txt + bytes, tmp, n);
        bytes += (int)n;
    }
    txt[bytes] = 0;
    return txt;
}

/* 返回 1 表示该位置命中了链接；open=1 时用系统浏览器打开 */
static int tv_link_at(TermView *tv, int line, int col, int open) {
    if (col < 0 || col >= tv->cols) return 0;
    int *map = (int *)xmalloc(sizeof(int) * (size_t)tv->cols);
    char *txt = tv_line_with_map(tv, line, map);
    if (!txt) { free(map); return 0; }
    int n = (int)strlen(txt);
    int start = map[col];
    if (start > n) start = n;
    int a = start, b = start;
    while (a > 0 && !link_sep(txt[a - 1])) a--;
    while (b < n && !link_sep(txt[b])) b++;
    /* 去掉结尾常见标点 */
    while (b > a && (txt[b - 1] == ',' || txt[b - 1] == '.' || txt[b - 1] == ';' ||
                     txt[b - 1] == ')' || txt[b - 1] == ']')) b--;
    char tok[2048];
    int len = b - a;
    int hit = 0;
    if (len > 0 && len < (int)sizeof(tok)) {
        memcpy(tok, txt + a, (size_t)len);
        tok[len] = 0;
        const char *url = NULL;
        if (!_strnicmp(tok, "http://", 7) || !_strnicmp(tok, "https://", 8) ||
            !_strnicmp(tok, "ftp://", 6) || !_strnicmp(tok, "file://", 7)) url = tok;
        else if (!_strnicmp(tok, "www.", 4)) url = tok;
        if (url) {
            hit = 1;
            if (open) {
                wchar_t *w = utf8_to_wide(url);
                if (w) {
                    wchar_t wurl[2048];
                    _snwprintf(wurl, 2048, L"%s%s",
                               (!_strnicmp(tok, "www.", 4) ? L"http://" : L""), w);
                    wurl[2047] = 0;
                    free(w);
                    ShellExecuteW(NULL, L"open", wurl, NULL, NULL, SW_SHOWNORMAL);
                }
            }
        }
    }
    free(txt);
    free(map);
    return hit;
}

/* ------------------------------------------------------------------ */
/* 拖放：本地会话插入路径；SSH 会话后台 scp 上传到远端家目录             */
/* ------------------------------------------------------------------ */
#define WM_YT_DROP_STATUS (WM_APP + 40)

typedef struct DropJob {
    HWND view;
    wchar_t files[16][MAX_PATH * 2];
    int n;
    char host[256];
    char user[96];
    char keyfile[MAX_PATH * 2];
    char password[256];
    int port;
    int auth;
} DropJob;

static void pass_helper_path(wchar_t *out, int cch) {
    wchar_t tmp[MAX_PATH];
    GetModuleFileNameW(NULL, tmp, MAX_PATH);
    wchar_t *slash = wcsrchr(tmp, L'\\');
    if (slash) {
        *slash = 0;
        _snwprintf(out, cch, L"%s\\YTPass.exe", tmp);
        out[cch - 1] = 0;
    } else {
        wcsncpy(out, L"YTPass.exe", (size_t)cch - 1);
        out[cch - 1] = 0;
    }
}

static void drop_set_askpass(DropJob *j) {
    if (j->auth == AUTH_PASSWORD && j->password[0]) {
        wchar_t helper[MAX_PATH];
        pass_helper_path(helper, MAX_PATH);
        SetEnvironmentVariableW(L"SSH_ASKPASS", helper);
        SetEnvironmentVariableW(L"SSH_ASKPASS_REQUIRE", L"force");
        SetEnvironmentVariableW(L"DISPLAY", L":0");
        wchar_t *pw = utf8_to_wide(j->password);
        if (pw) { SetEnvironmentVariableW(L"YT_ASKPASS", pw); free(pw); }
    } else {
        SetEnvironmentVariableW(L"SSH_ASKPASS", NULL);
        SetEnvironmentVariableW(L"SSH_ASKPASS_REQUIRE", NULL);
        SetEnvironmentVariableW(L"YT_ASKPASS", NULL);
    }
}

static DWORD WINAPI drop_upload_thread(LPVOID param) {
    DropJob *j = (DropJob *)param;
    drop_set_askpass(j);
    int ok = 0, fail = 0;
    wchar_t *user = utf8_to_wide(j->user);
    wchar_t *host = utf8_to_wide(j->host);
    wchar_t keyarg[600] = { 0 };
    if (j->auth == AUTH_KEY && j->keyfile[0]) {
        wchar_t *k = utf8_to_wide(j->keyfile);
        if (k) { _snwprintf(keyarg, 600, L"-i \"%s\" ", k); free(k); }
    }
    for (int i = 0; i < j->n; i++) {
        wchar_t cmd[4096];
        int batch = !(j->auth == AUTH_PASSWORD && j->password[0]);
        _snwprintf(cmd, 4096,
                   L"scp.exe -P %d -o StrictHostKeyChecking=accept-new -o ConnectTimeout=15 "
                   L"-o BatchMode=%s %s\"%s\" %s@%s:",
                   j->port > 0 ? j->port : 22,
                   batch ? L"yes" : L"no",
                   keyarg, j->files[i],
                   user ? user : L"", host ? host : L"");
        cmd[4095] = 0;
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        ZeroMemory(&pi, sizeof(pi));
        BOOL run = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
        int rc = -1;
        if (run) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 1;
            GetExitCodeProcess(pi.hProcess, &code);
            rc = (int)code;
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        if (rc == 0) ok++; else fail++;
    }
    free(user);
    free(host);
    wchar_t *msg = (wchar_t *)xmalloc(sizeof(wchar_t) * 256);
    if (fail == 0) _snwprintf(msg, 256, L"已上传 %d 个文件到远端家目录（~）", ok);
    else if (ok == 0) _snwprintf(msg, 256, L"拖放上传失败：%d 个文件未传成功，请检查网络或认证", fail);
    else _snwprintf(msg, 256, L"上传完成：成功 %d，失败 %d", ok, fail);
    msg[255] = 0;
    PostMessageW(j->view, WM_YT_DROP_STATUS, 0, (LPARAM)msg);
    free(j);
    return 0;
}

static void tv_drop_files(TermView *tv, HDROP drop) {
    if (!tv || !tv->back) { DragFinish(drop); return; }
    int n = (int)DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    if (n <= 0) { DragFinish(drop); return; }
    if (n > 16) n = 16;

    if (tv->s.protocol == PROTO_LOCAL) {
        /* 本地会话：把路径粘到命令行，带引号 */
        StrBuf sb;
        sb_init(&sb);
        for (int i = 0; i < n; i++) {
            wchar_t path[MAX_PATH * 2];
            if (!DragQueryFileW(drop, (UINT)i, path, MAX_PATH * 2)) continue;
            if (sb.len) sb_append(&sb, " ");
            sb_append(&sb, "\"");
            sb_appendw(&sb, path);
            sb_append(&sb, "\"");
        }
        if (sb.len) tv_send(tv, sb.buf, sb.len);
        sb_free(&sb);
        DragFinish(drop);
        return;
    }
    if (tv->s.protocol != PROTO_SSH) {
        app_status_set(L"该会话类型不支持拖放上传，请使用 SFTP 窗口");
        DragFinish(drop);
        return;
    }

    DropJob *j = (DropJob *)xcalloc(1, sizeof(DropJob));
    j->view = tv->hwnd;
    j->n = n;
    for (int i = 0; i < n; i++) {
        if (!DragQueryFileW(drop, (UINT)i, j->files[i], MAX_PATH * 2)) j->files[i][0] = 0;
    }
    strncpy(j->host, tv->s.host, sizeof(j->host) - 1);
    strncpy(j->user, tv->s.user, sizeof(j->user) - 1);
    strncpy(j->keyfile, tv->s.keyfile, sizeof(j->keyfile) - 1);
    strncpy(j->password, tv->s.password, sizeof(j->password) - 1);
    j->port = tv->s.port;
    j->auth = tv->s.auth;
    DragFinish(drop);
    HANDLE th = CreateThread(NULL, 0, drop_upload_thread, j, 0, NULL);
    if (th) CloseHandle(th);
    else free(j);
    wchar_t st[128];
    _snwprintf(st, 128, L"正在上传 %d 个文件…", n);
    app_status_set(st);
}

static void tv_mouse_hit(TermView *tv, int x, int y, int *line, int *col) {
    int px = x - tv->pad;
    int py = y - tv->pad;
    int cx = px / tv->cell_w;
    int cy = py / tv->cell_h;
    if (cx < 0) cx = 0;
    if (cx >= tv->cols) cx = tv->cols - 1;
    if (cy < 0) cy = 0;
    if (cy >= tv->rows) cy = tv->rows - 1;
    *col = cx;
    *line = vt_line_index_for_row(tv->vt, cy);
}

static int is_word_char(u32 cp) {
    if (cp >= '0' && cp <= '9') return 1;
    if (cp >= 'a' && cp <= 'z') return 1;
    if (cp >= 'A' && cp <= 'Z') return 1;
    if (cp == '_' || cp == '-' || cp == '.' || cp == '/' || cp == '\\' || cp == ':' || cp == '@') return 1;
    if (cp > 0x2E80) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 窗口过程                                                             */
/* ------------------------------------------------------------------ */
static LRESULT CALLBACK SearchEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_RETURN) {
            TermView *tv = (TermView *)GetWindowLongPtrW(GetParent(h), GWLP_USERDATA);
            if (tv) tv_find_next(tv, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            TermView *tv = (TermView *)GetWindowLongPtrW(GetParent(h), GWLP_USERDATA);
            if (tv) tv_show_search(tv, 0);
            return 0;
        }
        break;
    case WM_CHAR:
        if (wp == VK_RETURN || wp == 27) return 0;
        break;
    }
    return CallWindowProcW(g_search_old_proc, h, msg, wp, lp);
}

static LRESULT CALLBACK TermProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    TermView *tv = tv_from(h);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        tv = (TermView *)cs->lpCreateParams;
        tv->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)tv);
        tv->search = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
                                     0, 0, 10, 10, h, (HMENU)1, cs->hInstance, NULL);
        SendMessageW(tv->search, WM_SETFONT, (WPARAM)g_app.f_ui, TRUE);
        SendMessageW(tv->search, EM_SETCUEBANNER, TRUE, (LPARAM)L"输入查找内容，回车下一个 / Shift+回车上一个");
        g_search_old_proc = (WNDPROC)SetWindowLongPtrW(tv->search, GWLP_WNDPROC, (LONG_PTR)SearchEditProc);
        SetTimer(h, TIMER_BLINK, 530, NULL);
        DragAcceptFiles(h, TRUE);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h, TIMER_BLINK);
        tv_log_stop(tv);
        if (tv->bmp) DeleteObject(tv->bmp);
        if (tv->memdc) DeleteDC(tv->memdc);
        return 0;
    case WM_DROPFILES:
        tv_drop_files(tv, (HDROP)wp);
        return 0;
    case WM_ERASEBKGND:        return 1;
    case WM_SIZE:
        if (tv) {
            tv_recalc(tv, LOWORD(lp), HIWORD(lp));
            if (tv->search_visible) {
                RECT rc; GetClientRect(h, &rc);
                int sw = app_dpi(300), sh = app_dpi(28);
                SetWindowPos(tv->search, HWND_TOP, rc.right - sw - app_dpi(16), app_dpi(6), sw, sh, SWP_NOACTIVATE);
            }
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        if (tv) {
            if (!tv->memdc) {
                /* 由 tv_paint 处理，这里直接调用 */
            }
            tv_paint(tv);
            if (tv->memdc) BitBlt(hdc, 0, 0, rc.right, rc.bottom, tv->memdc, 0, 0, SRCCOPY);
        } else {
            FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SETFOCUS:
        tv->focused = 1;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_KILLFOCUS:
        tv->focused = 0;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(h);
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int sbw = app_dpi(14);
        RECT rc; GetClientRect(h, &rc);
        if (x >= rc.right - sbw) {
            tv->scroll_drag = 1;
            int total = vt_total_lines(tv->vt);
            int max_off = vt_history_len(tv->vt);
            if (total > 0) {
                int off = (int)((float)max_off * (float)y / (float)rc.bottom);
                vt_scroll(tv->vt, max_off - vt_view_offset(tv->vt) - off);
            }
            SetCapture(h);
            return 0;
        }
        int line, col;
        tv_mouse_hit(tv, x, y, &line, &col);
        if ((wp & MK_CONTROL) && tv_link_at(tv, line, col, 1)) return 0;
        if (vt_mouse_mode(tv->vt) && !(wp & MK_SHIFT)) {
            tv_mouse_report(tv, 0, x - tv->pad, y - tv->pad, 0, 0);
            return 0;
        }
        tv->mouse_down = 1;
        tv->sel_active = 1;
        tv->sel_line0 = tv->sel_line1 = line;
        tv->sel_col0 = tv->sel_col1 = col;
        tv->dbl_click = 0;
        SetCapture(h);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (vt_mouse_mode(tv->vt)) return 0;
        int line, col;
        tv_mouse_hit(tv, x, y, &line, &col);
        Cell *cells = vt_line(tv->vt, line);
        if (!cells) return 0;
        int a = col, b = col;
        while (a > 0 && is_word_char(cells[a - 1].cp)) a--;
        while (b < tv->cols - 1 && is_word_char(cells[b + 1].cp)) b++;
        tv->sel_line0 = tv->sel_line1 = line;
        tv->sel_col0 = a;
        tv->sel_col1 = b;
        tv->sel_active = 1;
        tv->dbl_click = 1;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int line, col;
        tv_mouse_hit(tv, x, y, &line, &col);
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            if (tv_link_at(tv, line, col, 0)) SetCursor(LoadCursorW(NULL, (LPCWSTR)(ULONG_PTR)IDC_HAND));
        }
        if (!tv->scroll_hover) {
            RECT rc; GetClientRect(h, &rc);
            if (x >= rc.right - app_dpi(16) && !tv->scroll_hover) {
                tv->scroll_hover = 1;
                InvalidateRect(h, NULL, FALSE);
            }
        }
        if (tv->scroll_drag) {
            RECT rc; GetClientRect(h, &rc);
            int max_off = vt_history_len(tv->vt);
            int off = (int)((float)max_off * (float)y / (float)rc.bottom);
            vt_scroll(tv->vt, max_off - vt_view_offset(tv->vt) - off);
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        if (tv->mouse_down) {
            tv->sel_line1 = line;
            tv->sel_col1 = col;
            tv->sel_active = 1;
            tv_sel_norm(tv);
            if (y < 0) { vt_scroll(tv->vt, 1); }
            if (y > tv->rows * tv->cell_h + tv->pad) { vt_scroll(tv->vt, -1); }
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (tv->scroll_drag) {
            tv->scroll_drag = 0;
            ReleaseCapture();
            return 0;
        }
        if (vt_mouse_mode(tv->vt) && !(wp & MK_SHIFT)) {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            tv_mouse_report(tv, 0, x - tv->pad, y - tv->pad, 1, 0);
            return 0;
        }
        tv->mouse_down = 0;
        if (tv->sel_active) {
            int line, col;
            tv_mouse_hit(tv, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &line, &col);
            tv->sel_line1 = line;
            tv->sel_col1 = col;
            if (tv->sel_line0 == tv->sel_line1 && tv->sel_col0 == tv->sel_col1) tv->sel_active = 0;
            tv_sel_norm(tv);
            if (tv->sel_active && g_app.set.copy_on_select) tv_copy_selection(tv);
        }
        ReleaseCapture();
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_RBUTTONUP: {
        if (vt_mouse_mode(tv->vt) && wp == 0) {
            tv_mouse_report(tv, 2, GET_X_LPARAM(lp) - tv->pad, GET_Y_LPARAM(lp) - tv->pad, 1, 0);
        }
        if (g_app.set.right_click_paste) {
            char *text = app_clipboard_get();
            if (text) {
                int len = (int)strlen(text);
                if (vt_bracket_paste(tv->vt)) {
                    /* 由 tv_paste 负责 */
                }
                if (len) tv_send(tv, text, len);
                free(text);
                return 0;
            }
        }
        tv_context_menu(tv);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        int notches = delta / WHEEL_DELTA;
        if (vt_mouse_mode(tv->vt)) {
            if (!(wp & MK_SHIFT)) {
                POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                ScreenToClient(h, &pt);
                tv_mouse_report(tv, notches > 0 ? 64 : 65, pt.x - tv->pad, pt.y - tv->pad, 0, 1);
                return 0;
            }
        }
        vt_scroll(tv->vt, notches * 3);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_BLINK) {
            tv->blink_on = !tv->blink_on;
            if (tv->focused) InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        return 0;
    case WM_KEYDOWN: {
        int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        int alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

        tv->suppress_char = 0;
        if (ctrl && shift) {
            tv->suppress_char = 1;
            switch (wp) {
            case 'C': tv_copy_selection(tv); return 0;
            case 'V': PostMessageW(g_app.main, WM_COMMAND, IDC_PASTE, 0); return 0;
            case 'F': tv_show_search(tv, 1); return 0;
            case 'L': PostMessageW(g_app.main, WM_COMMAND, IDC_LOG, 0); return 0;
            case 'D': PostMessageW(g_app.main, WM_COMMAND, IDC_SPLIT_V, 0); return 0;
            case 'E': PostMessageW(g_app.main, WM_COMMAND, IDC_SPLIT_H, 0); return 0;
            case 'K': PostMessageW(g_app.main, WM_COMMAND, IDC_QUICKCMD, 0); return 0;
            case 'S': PostMessageW(g_app.main, WM_COMMAND, IDC_SFTP, 0); return 0;
            case 'P': PostMessageW(g_app.main, WM_COMMAND, IDC_PALETTE, 0); return 0;
            default: tv->suppress_char = 0; break;
            }
        }
        if (ctrl) {
            switch (wp) {
            case 'W': PostMessageW(g_app.main, WM_COMMAND, IDC_CLOSE_TAB, 0); return 0;
            case VK_TAB: PostMessageW(g_app.main, WM_COMMAND, IDC_NEW_TAB, 0); return 0;
            case VK_OEM_PLUS: case VK_ADD: PostMessageW(g_app.main, WM_COMMAND, IDC_FONT_INC, 0); return 0;
            case VK_OEM_MINUS: case VK_SUBTRACT: PostMessageW(g_app.main, WM_COMMAND, IDC_FONT_DEC, 0); return 0;
            case '0': PostMessageW(g_app.main, WM_COMMAND, IDC_FONT_RESET, 0); return 0;
            case VK_F11: return 0;
            default: break;
            }
        }
        if (alt && !ctrl) {
            char suffix[4] = { 0 };
            if ((wp >= 'A' && wp <= 'Z') || (wp >= '0' && wp <= '9')) {
                char ch = (char)wp;
                if (shift || (GetKeyState(VK_CAPITAL) & 1)) { /* 大写 */ }
                else if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
                char seq[8] = { 0x1b, ch, 0 };
                tv_send(tv, seq, 2);
                tv->suppress_char = 1;
                return 0;
            }
            (void)suffix;
        }
        if (wp == VK_TAB && shift) { tv_send(tv, "\x1b[Z", 3); return 0; }
        char buf[24];
        size_t n = key_seq(tv, wp, ctrl, alt, shift, buf);
        if (n) { tv_send(tv, buf, n); if (ctrl) tv->suppress_char = 1; return 0; }
        if (wp == VK_SPACE && ctrl) { tv_send(tv, "\0", 1); tv->suppress_char = 1; return 0; }
        return 0;
    }
    case WM_CHAR: {
        if (tv->suppress_char) { tv->suppress_char = 0; return 0; }
        if (tv->search_visible && GetFocus() == tv->search) return 0;
        u32 ch = (u32)wp;
        if (ch == 0x0D) return 0;   /* 回车已在 WM_KEYDOWN 处理 */
        if (ch < 0x20 && ch != 0x09 && ch != 0x08) {
            char c = (char)ch;
            tv_send(tv, &c, 1);
            return 0;
        }
        if (ch == 0x09) { tv_send(tv, "\t", 1); return 0; }
        if (ch == 0x08) { tv_send(tv, "\x7f", 1); return 0; }
        char u8buf[8];
        size_t n = utf8_encode_one(ch, u8buf);
        tv_send(tv, u8buf, n);
        return 0;
    }
    case WM_IME_CHAR:
        /* 由 WM_CHAR 处理，避免重复 */
        return 0;
    case WM_IME_COMPOSITION:
        if (lp & GCS_RESULTSTR) {
            HIMC himc = ImmGetContext(h);
            if (himc) {
                int len = ImmGetCompositionStringW(himc, GCS_RESULTSTR, NULL, 0);
                if (len > 0) {
                    wchar_t *wbuf = (wchar_t *)xmalloc((size_t)len + 2);
                    ImmGetCompositionStringW(himc, GCS_RESULTSTR, wbuf, (DWORD)len);
                    wbuf[len / 2] = 0;
                    char *u8 = wide_to_utf8(wbuf);
                    if (u8 && *u8) tv_send(tv, u8, strlen(u8));
                    free(u8);
                    free(wbuf);
                }
                ImmReleaseContext(h, himc);
            }
        }
        return 0;
    case WM_YT_BACK_DATA: {
        BackData *d = (BackData *)lp;
        if (tv && tv->back && d && d->id == backend_id(tv->back)) {
            size_t ulen = 0;
            char *ustr = tv_decode_input(tv, d->data, d->len, &ulen);
            vt_feed(tv->vt, (const u8 *)ustr, ulen);
            if (tv->log_on) {
                /* 转成 UTF-8 文本写日志 */
                tv->log_pending[tv->log_plen] = 0;
                /* 直接按行缓冲 */
                size_t used = 0;
                char *tmp = (char *)xmalloc(ulen + 1);
                for (size_t i = 0; i < ulen; i++) {
                    unsigned char c = (unsigned char)ustr[i];
                    if (c == '\n') {
                        tmp[used] = 0;
                        if (tv->log_plen) {
                            memcpy(tv->log_pending + tv->log_plen, tmp, used);
                            tv->log_plen += (int)used;
                            tv->log_pending[tv->log_plen] = 0;
                            tv_log_flush_line(tv, tv->log_pending);
                            tv->log_plen = 0;
                        } else {
                            tv_log_flush_line(tv, tmp);
                        }
                        used = 0;
                    } else if (c != '\r') {
                        tmp[used++] = (char)c;
                        if (used >= ulen) break;
                    }
                }
                if (used) {
                    memcpy(tv->log_pending + tv->log_plen, tmp, used);
                    tv->log_plen += (int)used;
                    if (tv->log_plen > (int)sizeof(tv->log_pending) - 2) {
                        tv->log_pending[tv->log_plen] = 0;
                        tv_log_flush_line(tv, tv->log_pending);
                        tv->log_plen = 0;
                    }
                }
                free(tmp);
            }
            tv_triggers(tv, ustr ? ustr : "", ulen);
            free(ustr);
            free(d);
            InvalidateRect(h, NULL, FALSE);
        } else free(d);
        return 0;
    }
    case WM_YT_DROP_STATUS:
        if (lp) {
            app_status_set((const wchar_t *)lp);
            free((void *)lp);
        }
        return 0;
    case WM_YT_BACK_STATE: {
        if (tv && (int)wp == backend_id(tv->back)) {
            if ((int)lp == BS_CLOSED) {
                PostMessageW(g_app.main, WM_YT_VIEW_STATUS, (WPARAM)h, 0);
            }
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }
    case WM_YT_BACK_ERROR: {
        char *msg = (char *)lp;
        if (tv && msg) {
            char line[600];
            _snprintf(line, sizeof(line), "\r\n\x1b[1;31m[错误] %s\x1b[0m\r\n", msg);
            vt_feed(tv->vt, (const u8 *)line, strlen(line));
            free(msg);
            InvalidateRect(h, NULL, FALSE);
            PostMessageW(g_app.main, WM_YT_VIEW_STATUS, (WPARAM)h, 0);
        }
        return 0;
    }
    case WM_YT_APPLY_SETTINGS:
        if (tv) {
            RECT rc; GetClientRect(h, &rc);
            tv->cell_w = g_app.cell_w;
            tv->cell_h = g_app.cell_h;
            tv->ascent = g_app.font_ascent;
            tv->pad = app_dpi(4);
            tv_recalc(tv, rc.right, rc.bottom);
            if (tv->bmp) { DeleteObject(tv->bmp); tv->bmp = NULL; }
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* 创建/销毁                                                            */
/* ------------------------------------------------------------------ */
static void tv_register(HINSTANCE inst) {
    if (g_registered) return;
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = TermProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_IBEAM);
    wc.lpszClassName = TERM_CLASS;
    wc.hbrBackground = NULL;
    RegisterClassExW(&wc);
    g_registered = 1;
}

HWND term_view_create(HWND parent, Session *s, HINSTANCE inst) {
    tv_register(inst);
    TermView *tv = (TermView *)xcalloc(1, sizeof(TermView));
    tv->s = *s;
    tv->pad = app_dpi(4);
    tv->cell_w = g_app.cell_w;
    tv->cell_h = g_app.cell_h;
    tv->ascent = g_app.font_ascent;
    tv->cols = 120;
    tv->rows = 30;
    tv->blink_on = 1;
    tv->match_line = 0;
    tv->match_cells = 0;
    wcsncpy(tv->title, (wchar_t *)utf8_to_wide(s->name), 191);
    tv->title[191] = 0;
    tv->vt = vt_create(tv->cols, tv->rows, g_app.set.scrollback);
    vt_set_user(tv->vt, tv);
    vt_set_write_cb(tv->vt, cb_write);
    vt_set_bell_cb(tv->vt, cb_bell);
    vt_set_title_cb(tv->vt, cb_title);
    vt_set_notify_cb(tv->vt, cb_notify);
    vt_set_clip_cb(tv->vt, cb_clip);

    HWND h = CreateWindowExW(0, TERM_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_TABSTOP,
                             0, 0, 800, 500, parent, NULL, inst, tv);
    if (!h) { vt_destroy(tv->vt); free(tv); return NULL; }
    tv->back = backend_create(&tv->s, h, tv->cols, tv->rows);
    backend_start(tv->back);
    if (g_app.set.log_auto) tv_log_start(tv);
    return h;
}

void term_view_destroy(HWND h) {
    TermView *tv = tv_from(h);
    if (!tv) return;
    tv->closing = 1;
    if (tv->back) { backend_close(tv->back); tv->back = NULL; }
    if (tv->vt) { vt_destroy(tv->vt); tv->vt = NULL; }
    tv_log_stop(tv);
    SetWindowLongPtrW(h, GWLP_USERDATA, 0);
    DestroyWindow(h);
    free(tv);
}

Session *term_view_session(HWND h) { TermView *tv = tv_from(h); return tv ? &tv->s : NULL; }
Backend *term_view_backend(HWND h) { TermView *tv = tv_from(h); return tv ? tv->back : NULL; }
int term_view_cols(HWND h) { TermView *tv = tv_from(h); return tv ? tv->cols : 0; }
int term_view_rows(HWND h) { TermView *tv = tv_from(h); return tv ? tv->rows : 0; }
const wchar_t *term_view_title(HWND h) { TermView *tv = tv_from(h); return tv ? tv->title : L""; }
void term_view_set_title(HWND h, const wchar_t *title) { TermView *tv = tv_from(h); if (!tv) return; wcsncpy(tv->title, title ? title : L"", 191); tv->title[191] = 0; tv->title_custom = 1; PostMessageW(g_app.main, WM_YT_TITLE_CHANGED, (WPARAM)h, 0); }
void term_view_focus(HWND h) { SetFocus(h); }
void term_view_send_text(HWND h, const char *text, size_t len) {
    TermView *tv = tv_from(h);
    if (!tv || !tv->back) return;
    if (vt_bracket_paste(tv->vt) && len > 1) {
        tv_write_utf8(tv, "\x1b[200~", 6);
        tv_write_utf8(tv, text, len);
        tv_write_utf8(tv, "\x1b[201~", 6);
    } else {
        tv_write_utf8(tv, text, len);
    }
}
/* 广播输入：收到的永远是 UTF-8，由各视图按自己的编码转换后写入 */
void term_view_broadcast_text(HWND h, const char *text, size_t len) {
    TermView *tv = tv_from(h);
    if (!tv || !tv->back) return;
    if (vt_bracket_paste(tv->vt) && len > 1) {
        tv_write_utf8(tv, "\x1b[200~", 6);
        tv_write_utf8(tv, text, len);
        tv_write_utf8(tv, "\x1b[201~", 6);
    } else {
        tv_write_utf8(tv, text, len);
    }
}
void term_view_toggle_log(HWND h) {
    TermView *tv = tv_from(h);
    if (!tv) return;
    if (tv->log_on) tv_log_stop(tv);
    else tv_log_start(tv);
}
int term_view_is_logging(HWND h) { TermView *tv = tv_from(h); return tv ? tv->log_on : 0; }
void term_view_search(HWND h) { TermView *tv = tv_from(h); if (tv) tv_show_search(tv, 1); }
void term_view_clear(HWND h) {
    TermView *tv = tv_from(h);
    if (!tv) return;
    vt_feed(tv->vt, (const u8 *)"\x1b[2J\x1b[H", 7);
    vt_clear_history(tv->vt);
    InvalidateRect(h, NULL, FALSE);
}
void term_view_select_all(HWND h) {
    TermView *tv = tv_from(h);
    if (!tv) return;
    tv->sel_active = 1;
    tv->sel_line0 = 0;
    tv->sel_col0 = 0;
    tv->sel_line1 = vt_total_lines(tv->vt) - 1;
    tv->sel_col1 = tv->cols - 1;
    InvalidateRect(h, NULL, FALSE);
}
void term_view_copy(HWND h) {
    TermView *tv = tv_from(h);
    if (tv) tv_copy_selection(tv);
}
int term_view_has_selection(HWND h) {
    TermView *tv = tv_from(h);
    return tv ? tv->sel_active : 0;
}
void term_view_paste_clipboard(HWND h) {
    TermView *tv = tv_from(h);
    if (!tv) return;
    char *text = app_clipboard_get();
    if (!text) return;
    size_t len = strlen(text);
    if (vt_bracket_paste(tv->vt)) {
        backend_write(tv->back, "\x1b[200~", 6);
        backend_write(tv->back, text, len);
        backend_write(tv->back, "\x1b[201~", 6);
    } else {
        backend_write(tv->back, text, len);
    }
    free(text);
    InvalidateRect(h, NULL, FALSE);
}
void term_view_reconnect(HWND h) {
    TermView *tv = tv_from(h);
    if (!tv) return;
    if (tv->back) backend_close(tv->back);
    tv->back = backend_create(&tv->s, h, tv->cols, tv->rows);
    backend_start(tv->back);
    InvalidateRect(h, NULL, FALSE);
}
const wchar_t *term_view_status_text(HWND h) {
    static wchar_t buf[256];
    TermView *tv = tv_from(h);
    if (!tv || !tv->back) { wcsncpy(buf, L"未连接", 255); buf[255] = 0; return buf; }
    wchar_t *w = utf8_to_wide(backend_status(tv->back));
    wcsncpy(buf, w, 255); buf[255] = 0;
    free(w);
    return buf;
}
int term_view_state(HWND h) { TermView *tv = tv_from(h); return (tv && tv->back) ? backend_state(tv->back) : BS_IDLE; }

/* 光标 y 辅助需要在 tv_paint_cells 之前声明 */
