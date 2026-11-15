#include "yt_core.h"

#define IDM_T_COPY  9001
#define IDM_T_PASTE 9002
#define IDM_T_ALL   9003
#define IDM_T_CLR   9004
#define IDT_REDRAW  9101
#define IDT_STATUS  9102

typedef struct {
    wchar_t **lines;
    int *line_len;
    BYTE **fg;
    int line_count, cols, rows, cx, cy;
    BYTE cur_fg, cur_bg;
    int ansi_state, ansi_len;
    char ansi_buf[64];
    unsigned char ub[4];
    int un, uneed;
    YtSession *sess;
    HFONT font;
    int font_px;
    int chw, chh, scroll;
    int find_from;
    wchar_t find[128];
    int sel_r0, sel_c0, sel_r1, sel_c1;
    BOOL selecting;
    BOOL dirty;
    BOOL status_dirty;
    /* cached backbuffer */
    HDC mem_dc;
    HBITMAP mem_bmp;
    int mem_w, mem_h;
} Term;

#define TERM_CLASS L"YTermViewPro"
#define PAD_X 6
#define PAD_Y 4
#define BG RGB(12, 12, 12)

static Term *T(HWND hw) { return (Term *)GetWindowLongPtrW(hw, GWLP_USERDATA); }

static COLORREF pal(BYTE i)
{
    static const COLORREF c[16] = {
        RGB(12,12,12), RGB(197,15,31), RGB(19,161,14), RGB(193,156,0),
        RGB(0,55,218), RGB(136,23,152), RGB(58,150,221), RGB(204,204,204),
        RGB(118,118,118), RGB(231,72,86), RGB(22,198,12), RGB(249,241,165),
        RGB(59,120,255), RGB(180,0,158), RGB(97,214,214), RGB(242,242,242)
    };
    return c[i & 15];
}

static void ensure_lines(Term *t, int need)
{
    while (t->line_count < need) {
        if (t->line_count >= YT_SCROLLBACK) {
            free(t->lines[0]); free(t->fg[0]);
            memmove(t->lines, t->lines + 1, sizeof(void *) * (YT_SCROLLBACK - 1));
            memmove(t->fg, t->fg + 1, sizeof(void *) * (YT_SCROLLBACK - 1));
            memmove(t->line_len, t->line_len + 1, sizeof(int) * (YT_SCROLLBACK - 1));
            t->line_count = YT_SCROLLBACK - 1;
            if (t->cy > 0) t->cy--;
            if (t->sel_r0 > 0) t->sel_r0--;
            if (t->sel_r1 > 0) t->sel_r1--;
        }
        t->lines[t->line_count] = (wchar_t *)calloc(t->cols + 1, sizeof(wchar_t));
        t->fg[t->line_count] = (BYTE *)calloc(t->cols + 1, 1);
        t->line_len[t->line_count] = 0;
        t->line_count++;
    }
}

static void put_char(Term *t, wchar_t ch)
{
    ensure_lines(t, t->cy + 1);
    if (ch == L'\r') { t->cx = 0; return; }
    if (ch == L'\n') { t->cx = 0; t->cy++; ensure_lines(t, t->cy + 1); return; }
    if (ch == L'\b') { if (t->cx > 0) t->cx--; return; }
    if (ch == L'\t') { int n = 8 - (t->cx % 8); while (n--) put_char(t, L' '); return; }
    if (ch < 32) return;
    if (t->cx >= t->cols) { t->cx = 0; t->cy++; ensure_lines(t, t->cy + 1); }
    t->lines[t->cy][t->cx] = ch;
    t->fg[t->cy][t->cx] = t->cur_fg;
    if (t->cx + 1 > t->line_len[t->cy]) t->line_len[t->cy] = t->cx + 1;
    t->cx++;
}

static void ansi_sgr(Term *t, int *p, int n)
{
    int i;
    if (n == 0) { t->cur_fg = 7; t->cur_bg = 0; return; }
    for (i = 0; i < n; i++) {
        int v = p[i];
        if (v == 0) { t->cur_fg = 7; t->cur_bg = 0; }
        else if (v >= 30 && v <= 37) t->cur_fg = (BYTE)(v - 30);
        else if (v == 39) t->cur_fg = 7;
        else if (v >= 90 && v <= 97) t->cur_fg = (BYTE)(v - 90 + 8);
        else if (v >= 40 && v <= 47) t->cur_bg = (BYTE)(v - 40);
    }
}

static void erase_line(Term *t, int mode)
{
    int i;
    ensure_lines(t, t->cy + 1);
    if (mode == 2) {
        memset(t->lines[t->cy], 0, (t->cols + 1) * sizeof(wchar_t));
        memset(t->fg[t->cy], 0, t->cols + 1);
        t->line_len[t->cy] = 0;
    } else if (mode == 1) {
        for (i = 0; i <= t->cx && i < t->cols; i++) {
            t->lines[t->cy][i] = L' ';
            t->fg[t->cy][i] = t->cur_fg;
        }
        if (t->line_len[t->cy] < t->cx + 1) t->line_len[t->cy] = t->cx + 1;
    } else {
        for (i = t->cx; i < t->cols; i++) {
            t->lines[t->cy][i] = 0;
            t->fg[t->cy][i] = 0;
        }
        t->line_len[t->cy] = t->cx;
    }
}

static void ansi_exec(Term *t, char final)
{
    int params[8] = {0}, n = 0, v = 0, got = 0, i;
    char *s = t->ansi_buf;
    while (*s && n < 8) {
        if (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); got = 1; }
        else if (*s == ';') { params[n++] = got ? v : 0; v = 0; got = 0; }
        s++;
    }
    if (n < 8 && (got || n == 0)) params[n++] = got ? v : 0;
    if (final == 'm') ansi_sgr(t, params, n);
    else if (final == 'H' || final == 'f') {
        /* CUP is screen-relative to the bottom rows×cols window, not absolute scrollback */
        int row = params[0] > 0 ? params[0] - 1 : 0;
        int col = (n > 1 && params[1] > 0) ? params[1] - 1 : 0;
        int base;
        if (row >= t->rows) row = t->rows - 1;
        if (row < 0) row = 0;
        ensure_lines(t, t->rows);
        base = t->line_count - t->rows;
        if (base < 0) base = 0;
        t->cy = base + row;
        t->cx = col;
        if (t->cx < 0) t->cx = 0;
        if (t->cx > t->cols) t->cx = t->cols;
        ensure_lines(t, t->cy + 1);
    } else if (final == 'J') {
        if (params[0] == 2 || params[0] == 3) {
            for (i = 0; i < t->line_count; i++) { free(t->lines[i]); free(t->fg[i]); }
            t->line_count = 0; t->cx = t->cy = 0; ensure_lines(t, 1);
            t->sel_r0 = t->sel_r1 = -1;
        } else if (params[0] == 0) {
            erase_line(t, 0);
            for (i = t->cy + 1; i < t->line_count; i++) {
                memset(t->lines[i], 0, (t->cols + 1) * sizeof(wchar_t));
                t->line_len[i] = 0;
            }
        }
    } else if (final == 'K') erase_line(t, params[0]);
    else if (final == 'A') {
        int base = t->line_count - t->rows; if (base < 0) base = 0;
        t->cy -= params[0] ? params[0] : 1;
        if (t->cy < base) t->cy = base;
    } else if (final == 'B') {
        int base = t->line_count - t->rows; if (base < 0) base = 0;
        t->cy += params[0] ? params[0] : 1;
        if (t->cy > base + t->rows - 1) t->cy = base + t->rows - 1;
        ensure_lines(t, t->cy + 1);
    } else if (final == 'C') { t->cx += params[0] ? params[0] : 1; if (t->cx > t->cols) t->cx = t->cols; }
    else if (final == 'D') { t->cx -= params[0] ? params[0] : 1; if (t->cx < 0) t->cx = 0; }
    else if (final == 'G') { t->cx = (params[0] > 0 ? params[0] - 1 : 0); if (t->cx > t->cols) t->cx = t->cols; }
}

static void feed_byte(Term *t, unsigned char c)
{
    if (t->ansi_state == 1) {
        if (c == '[') { t->ansi_state = 2; t->ansi_len = 0; return; }
        if (c == ']') { t->ansi_state = 3; return; } /* OSC ignore until BEL/ST */
        t->ansi_state = 0;
    }
    if (t->ansi_state == 3) {
        if (c == 0x07 || c == 0x1b) t->ansi_state = (c == 0x1b) ? 4 : 0;
        return;
    }
    if (t->ansi_state == 4) { t->ansi_state = 0; return; } /* eat ST '\' */
    if (t->ansi_state == 2) {
        if ((c >= '0' && c <= '9') || c == ';' || c == '?' || c == ' ') {
            if (t->ansi_len < 63) t->ansi_buf[t->ansi_len++] = (char)c;
            return;
        }
        t->ansi_buf[t->ansi_len] = 0;
        ansi_exec(t, (char)c);
        t->ansi_state = 0;
        return;
    }
    if (c == 0x1b) { t->ansi_state = 1; return; }
    if (t->uneed) {
        t->ub[t->un++] = c;
        if (t->un >= t->uneed) {
            wchar_t w;
            if (t->uneed == 2)
                w = (wchar_t)(((t->ub[0] & 0x1f) << 6) | (t->ub[1] & 0x3f));
            else
                w = (wchar_t)(((t->ub[0] & 0x0f) << 12) | ((t->ub[1] & 0x3f) << 6) | (t->ub[2] & 0x3f));
            put_char(t, w);
            t->un = t->uneed = 0;
        }
        return;
    }
    if (c < 0x80) { put_char(t, (wchar_t)c); return; }
    if ((c & 0xe0) == 0xc0) { t->ub[0] = c; t->un = 1; t->uneed = 2; return; }
    if ((c & 0xf0) == 0xe0) { t->ub[0] = c; t->un = 1; t->uneed = 3; return; }
    if ((c & 0xf8) == 0xf0) { t->un = t->uneed = 0; put_char(t, L'?'); return; }
    put_char(t, L'?');
}

static void schedule_redraw(HWND hw, Term *t)
{
    if (t->dirty) return;
    t->dirty = TRUE;
    SetTimer(hw, IDT_REDRAW, 16, NULL); /* ~60fps */
}

void term_append(HWND hw, const char *utf8, int len)
{
    Term *t = T(hw); int i;
    if (!t || !utf8 || len <= 0) return;
    for (i = 0; i < len; i++) feed_byte(t, (unsigned char)utf8[i]);
    t->scroll = 0;
    schedule_redraw(hw, t);
    if (!t->status_dirty) {
        t->status_dirty = TRUE;
        SetTimer(hw, IDT_STATUS, 200, NULL);
    }
}

void term_clear(HWND hw)
{
    Term *t = T(hw); int i;
    if (!t) return;
    for (i = 0; i < t->line_count; i++) { free(t->lines[i]); free(t->fg[i]); }
    t->line_count = 0; t->cx = t->cy = 0; t->sel_r0 = t->sel_r1 = -1;
    ensure_lines(t, 1);
    t->dirty = FALSE;
    InvalidateRect(hw, NULL, FALSE);
}

void term_find(HWND hw, const wchar_t *text)
{
    Term *t = T(hw); int i;
    if (!t) return;
    wcsncpy(t->find, text ? text : L"", 127);
    t->find_from = t->line_count;
    if (t->find[0]) {
        for (i = t->line_count - 1; i >= 0; i--) {
            if (wcsstr(t->lines[i], t->find)) {
                t->find_from = i;
                t->scroll = t->line_count - i - 30;
                if (t->scroll < 0) t->scroll = 0;
                break;
            }
        }
    }
    InvalidateRect(hw, NULL, FALSE);
}

void term_find_next(HWND hw)
{
    Term *t = T(hw); int i, start;
    if (!t || !t->find[0]) return;
    start = t->find_from > 0 ? t->find_from - 1 : t->line_count - 1;
    for (i = start; i >= 0; i--) {
        if (wcsstr(t->lines[i], t->find)) {
            t->find_from = i;
            t->scroll = t->line_count - i - 30;
            if (t->scroll < 0) t->scroll = 0;
            InvalidateRect(hw, NULL, FALSE);
            return;
        }
    }
    t->find_from = t->line_count;
}

void term_set_font_px(HWND hw, int px)
{
    Term *t = T(hw); HDC hdc; TEXTMETRICW tm;
    if (!t) return;
    if (px < 10) px = 10;
    if (px > 36) px = 36;
    t->font_px = px;
    if (t->font) DeleteObject(t->font);
    t->font = CreateFontW(-px, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    hdc = GetDC(hw); SelectObject(hdc, t->font); GetTextMetricsW(hdc, &tm);
    t->chw = tm.tmAveCharWidth; t->chh = tm.tmHeight; ReleaseDC(hw, hdc);
    if (t->mem_bmp) { DeleteObject(t->mem_bmp); t->mem_bmp = NULL; }
    if (t->mem_dc) { DeleteDC(t->mem_dc); t->mem_dc = NULL; }
    InvalidateRect(hw, NULL, TRUE);
}

int term_get_font_px(HWND hw)
{
    Term *t = T(hw);
    return t ? t->font_px : 15;
}

void term_attach(HWND hw, YtSession *s) { Term *t = T(hw); if (t) t->sess = s; }
YtSession *term_session(HWND hw) { Term *t = T(hw); return t ? t->sess : NULL; }
void term_set_focus(HWND hw) { SetFocus(hw); }

BOOL term_dump(HWND hw, const wchar_t *path)
{
    Term *t = T(hw); FILE *fp; int i; char up[YT_PATH_MAX];
    if (!t || !path) return FALSE;
    WideCharToMultiByte(CP_UTF8, 0, path, -1, up, sizeof(up), NULL, NULL);
    fp = fopen(up, "wb");
    if (!fp) return FALSE;
    for (i = 0; i < t->line_count; i++) {
        char u8[YT_COLS * 3 + 4];
        int n = WideCharToMultiByte(CP_UTF8, 0, t->lines[i], t->line_len[i], u8, sizeof(u8) - 2, NULL, NULL);
        if (n > 0) { u8[n] = '\n'; fwrite(u8, 1, n + 1, fp); }
    }
    fclose(fp);
    return TRUE;
}

void term_get_cursor(HWND hw, int *row, int *col, int *lines)
{
    Term *t = T(hw);
    if (!t) { if (row) *row = 0; if (col) *col = 0; if (lines) *lines = 0; return; }
    if (row) *row = t->cy + 1;
    if (col) *col = t->cx + 1;
    if (lines) *lines = t->line_count;
}

static int pos_cmp(int r0, int c0, int r1, int c1)
{
    if (r0 != r1) return r0 - r1;
    return c0 - c1;
}

static void norm_sel(Term *t, int *r0, int *c0, int *r1, int *c1)
{
    *r0 = t->sel_r0; *c0 = t->sel_c0; *r1 = t->sel_r1; *c1 = t->sel_c1;
    if (pos_cmp(*r0, *c0, *r1, *c1) > 0) {
        int tr = *r0, tc = *c0; *r0 = *r1; *c0 = *c1; *r1 = tr; *c1 = tc;
    }
}

static BOOL in_sel(Term *t, int r, int c)
{
    int r0, c0, r1, c1;
    if (t->sel_r0 < 0 || t->sel_r1 < 0) return FALSE;
    norm_sel(t, &r0, &c0, &r1, &c1);
    if (r < r0 || r > r1) return FALSE;
    if (r0 == r1) return c >= c0 && c < c1;
    if (r == r0) return c >= c0;
    if (r == r1) return c < c1;
    return TRUE;
}

static void cell_from_pt(Term *t, HWND hw, int x, int y, int *r, int *c)
{
    RECT rc; int vis, start, row;
    GetClientRect(hw, &rc);
    vis = t->chh ? (rc.bottom - PAD_Y) / t->chh : 30;
    start = t->line_count - vis - t->scroll;
    if (start < 0) start = 0;
    row = (y - PAD_Y) / (t->chh ? t->chh : 16);
    *r = start + row;
    *c = (x - PAD_X) / (t->chw ? t->chw : 8);
    if (*r < 0) *r = 0;
    if (*c < 0) *c = 0;
    if (*c > t->cols) *c = t->cols;
}

static void clipboard_set(HWND hw, const wchar_t *buf, int total)
{
    if (!OpenClipboard(hw)) return;
    {
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (total + 1) * sizeof(wchar_t));
        if (h) {
            memcpy(GlobalLock(h), buf, (total + 1) * sizeof(wchar_t));
            GlobalUnlock(h); EmptyClipboard(); SetClipboardData(CF_UNICODETEXT, h);
        }
    }
    CloseClipboard();
}

static void copy_sel(HWND hw)
{
    Term *t = T(hw);
    int r0, c0, r1, c1, r, total = 0;
    wchar_t *buf;
    if (!t || t->sel_r0 < 0) return;
    norm_sel(t, &r0, &c0, &r1, &c1);
    if (r0 < 0) r0 = 0;
    if (r1 >= t->line_count) r1 = t->line_count - 1;
    for (r = r0; r <= r1; r++) total += t->line_len[r] + 2;
    buf = (wchar_t *)malloc((total + 1) * sizeof(wchar_t));
    if (!buf) return;
    total = 0;
    for (r = r0; r <= r1; r++) {
        int a = (r == r0) ? c0 : 0;
        int b = (r == r1) ? c1 : t->line_len[r];
        if (b > t->line_len[r]) b = t->line_len[r];
        if (a < b) {
            memcpy(buf + total, t->lines[r] + a, (b - a) * sizeof(wchar_t));
            total += b - a;
        }
        if (r < r1) { buf[total++] = L'\r'; buf[total++] = L'\n'; }
    }
    buf[total] = 0;
    clipboard_set(hw, buf, total);
    free(buf);
}

static void copy_all(HWND hw)
{
    Term *t = T(hw); int i, total = 0; wchar_t *buf;
    if (!t) return;
    for (i = 0; i < t->line_count; i++) total += t->line_len[i] + 2;
    buf = (wchar_t *)malloc((total + 1) * sizeof(wchar_t));
    if (!buf) return;
    total = 0;
    for (i = 0; i < t->line_count; i++) {
        memcpy(buf + total, t->lines[i], t->line_len[i] * sizeof(wchar_t));
        total += t->line_len[i]; buf[total++] = L'\r'; buf[total++] = L'\n';
    }
    buf[total] = 0;
    clipboard_set(hw, buf, total);
    free(buf);
}

static void paste(HWND hw)
{
    Term *t = T(hw); HANDLE h; wchar_t *w; char *u8; int need, delay, i;
    if (!t || !t->sess || t->sess->locked || !OpenClipboard(hw)) return;
    h = GetClipboardData(CF_UNICODETEXT);
    if (!h) { CloseClipboard(); return; }
    w = (wchar_t *)GlobalLock(h);
    need = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    u8 = (char *)malloc(need);
    delay = yt_get_paste_delay();
    if (u8) {
        WideCharToMultiByte(CP_UTF8, 0, w, -1, u8, need, NULL, NULL);
        if (delay <= 0) {
            yt_session_write(t->sess, u8, (int)strlen(u8));
            if (t->sess->sync_input) yt_app_sync_write(t->sess, u8, (int)strlen(u8));
        } else {
            for (i = 0; u8[i]; i++) {
                yt_session_write(t->sess, u8 + i, 1);
                if (t->sess->sync_input) yt_app_sync_write(t->sess, u8 + i, 1);
                Sleep((DWORD)delay);
            }
        }
        free(u8);
    }
    GlobalUnlock(h); CloseClipboard();
}

static void send_keys(Term *t, const char *data, int len)
{
    if (!t || !t->sess || t->sess->locked) return;
    yt_session_write(t->sess, data, len);
    if (t->sess->sync_input) yt_app_sync_write(t->sess, data, len);
}

void term_copy(HWND hw) { copy_sel(hw); }
void term_paste(HWND hw) { paste(hw); }
void term_select_all(HWND hw)
{
    Term *t = T(hw);
    if (!t || t->line_count <= 0) return;
    t->sel_r0 = 0; t->sel_c0 = 0;
    t->sel_r1 = t->line_count - 1;
    t->sel_c1 = t->line_len[t->sel_r1];
    copy_sel(hw);
    InvalidateRect(hw, NULL, FALSE);
}

static void ensure_backbuffer(Term *t, HDC hdc, int w, int h)
{
    if (t->mem_dc && t->mem_w == w && t->mem_h == h) return;
    if (t->mem_bmp) { DeleteObject(t->mem_bmp); t->mem_bmp = NULL; }
    if (t->mem_dc) { DeleteDC(t->mem_dc); t->mem_dc = NULL; }
    t->mem_dc = CreateCompatibleDC(hdc);
    t->mem_bmp = CreateCompatibleBitmap(hdc, w, h);
    SelectObject(t->mem_dc, t->mem_bmp);
    t->mem_w = w; t->mem_h = h;
}

/* Run-length paint: one ExtTextOutW per same-color span — not per cell */
static void paint(HWND hw)
{
    Term *t = T(hw);
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hw, &ps);
    RECT rc; GetClientRect(hw, &rc);
    HBRUSH bg;
    int vis, start, row, col;
    HGDIOBJ oldf;
    if (!t) { EndPaint(hw, &ps); return; }
    ensure_backbuffer(t, hdc, rc.right > 1 ? rc.right : 1, rc.bottom > 1 ? rc.bottom : 1);
    bg = CreateSolidBrush(BG);
    FillRect(t->mem_dc, &rc, bg);
    DeleteObject(bg);
    oldf = SelectObject(t->mem_dc, t->font);
    SetBkMode(t->mem_dc, OPAQUE);
    vis = t->chh ? (rc.bottom - PAD_Y) / t->chh : 30;
    if (vis < 1) vis = 1;
    start = t->line_count - vis - t->scroll;
    if (start < 0) start = 0;
    for (row = 0; row < vis; row++) {
        int li = start + row;
        int y = row * t->chh + PAD_Y;
        int len;
        if (li < 0 || li >= t->line_count) continue;
        len = t->line_len[li];
        col = 0;
        while (col < len) {
            BYTE f = t->fg[li][col] ? t->fg[li][col] : 7;
            BOOL sel = in_sel(t, li, col);
            int run = 1;
            while (col + run < len) {
                BYTE f2 = t->fg[li][col + run] ? t->fg[li][col + run] : 7;
                BOOL s2 = in_sel(t, li, col + run);
                if (f2 != f || s2 != sel) break;
                run++;
            }
            SetTextColor(t->mem_dc, sel ? RGB(0, 0, 0) : pal(f));
            SetBkColor(t->mem_dc, sel ? RGB(180, 200, 255) :
                       (t->find[0] && wcsstr(t->lines[li], t->find) ? RGB(80, 60, 0) : BG));
            ExtTextOutW(t->mem_dc, col * t->chw + PAD_X, y, ETO_OPAQUE, NULL,
                        t->lines[li] + col, run, NULL);
            col += run;
        }
        if (li == t->cy && t->scroll == 0) {
            RECT cr = {
                t->cx * t->chw + PAD_X, y,
                (t->cx + 1) * t->chw + PAD_X, y + t->chh
            };
            InvertRect(t->mem_dc, &cr);
        }
    }
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, t->mem_dc, 0, 0, SRCCOPY);
    SelectObject(t->mem_dc, oldf);
    EndPaint(hw, &ps);
}

static void ctx_menu(HWND hw, int x, int y)
{
    HMENU m = CreatePopupMenu();
    POINT pt = { x, y };
    AppendMenuW(m, MF_STRING, IDM_T_COPY, L"复制\tCtrl+Shift+C");
    AppendMenuW(m, MF_STRING, IDM_T_PASTE, L"粘贴\tCtrl+V");
    AppendMenuW(m, MF_STRING, IDM_T_ALL, L"全选");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_T_CLR, L"清屏");
    ClientToScreen(hw, &pt);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hw, NULL);
    DestroyMenu(m);
}

static LRESULT CALLBACK TermProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    Term *t = T(hw);
    switch (msg) {
    case WM_CREATE: {
        Term *nt = (Term *)calloc(1, sizeof(Term));
        TEXTMETRICW tm; HDC hdc;
        nt->cols = YT_COLS; nt->rows = YT_ROWS; nt->cur_fg = 7;
        nt->sel_r0 = nt->sel_r1 = -1;
        nt->lines = (wchar_t **)calloc(YT_SCROLLBACK, sizeof(wchar_t *));
        nt->fg = (BYTE **)calloc(YT_SCROLLBACK, sizeof(BYTE *));
        nt->line_len = (int *)calloc(YT_SCROLLBACK, sizeof(int));
        nt->font_px = 15;
        nt->find_from = -1;
        nt->font = CreateFontW(-nt->font_px, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)nt);
        ensure_lines(nt, 1);
        hdc = GetDC(hw); SelectObject(hdc, nt->font); GetTextMetricsW(hdc, &tm);
        nt->chw = tm.tmAveCharWidth; nt->chh = tm.tmHeight; ReleaseDC(hw, hdc);
        return 0;
    }
    case WM_YT_OUTPUT: {
        char *p = (char *)lp; int n = (int)wp;
        if (p) { term_append(hw, p, n); free(p); }
        return 0;
    }
    case WM_TIMER:
        if (wp == IDT_REDRAW && t) {
            KillTimer(hw, IDT_REDRAW);
            t->dirty = FALSE;
            InvalidateRect(hw, NULL, FALSE);
        } else if (wp == IDT_STATUS && t) {
            KillTimer(hw, IDT_STATUS);
            t->status_dirty = FALSE;
            PostMessageW(GetParent(hw), WM_YT_TITLE, 0, 0);
        }
        return 0;
    case WM_PAINT: paint(hw); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
        if (t) {
            RECT rc; int cols, rows;
            if (t->mem_bmp) {
                DeleteObject(t->mem_bmp); t->mem_bmp = NULL;
                DeleteDC(t->mem_dc); t->mem_dc = NULL;
                t->mem_w = t->mem_h = 0;
            }
            GetClientRect(hw, &rc);
            /* 隐藏标签时宽高为 0，勿 ResizePseudoConsole */
            if (t->chw > 0 && t->chh > 0 && rc.right > 40 && rc.bottom > 40) {
                cols = (rc.right - PAD_X) / t->chw;
                rows = (rc.bottom - PAD_Y) / t->chh;
                if (cols < 20) cols = 20;
                if (rows < 5) rows = 5;
                if (cols > 400) cols = 400;
                if (rows != t->rows || cols != t->cols) {
                    /* grow line buffers if needed */
                    if (cols > t->cols) {
                        int i;
                        for (i = 0; i < t->line_count; i++) {
                            t->lines[i] = (wchar_t *)realloc(t->lines[i], (cols + 1) * sizeof(wchar_t));
                            t->fg[i] = (BYTE *)realloc(t->fg[i], cols + 1);
                            if (t->lines[i]) ZeroMemory(t->lines[i] + t->cols, (cols - t->cols + 1) * sizeof(wchar_t));
                            if (t->fg[i]) ZeroMemory(t->fg[i] + t->cols, cols - t->cols + 1);
                        }
                    }
                    t->cols = cols;
                    t->rows = rows;
                    if (t->sess) yt_session_resize(t->sess, cols, rows);
                }
            }
        }
        return 0;
    case WM_LBUTTONDOWN: {
        int r, c;
        if (!t) return 0;
        SetCapture(hw); SetFocus(hw);
        cell_from_pt(t, hw, LOWORD(lp), HIWORD(lp), &r, &c);
        t->selecting = TRUE;
        t->sel_r0 = t->sel_r1 = r;
        t->sel_c0 = t->sel_c1 = c;
        InvalidateRect(hw, NULL, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (t && t->selecting && (wp & MK_LBUTTON)) {
            int r, c;
            cell_from_pt(t, hw, (short)LOWORD(lp), (short)HIWORD(lp), &r, &c);
            t->sel_r1 = r; t->sel_c1 = c;
            InvalidateRect(hw, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (t) {
            t->selecting = FALSE;
            ReleaseCapture();
            if (t->sel_r0 == t->sel_r1 && t->sel_c0 == t->sel_c1)
                t->sel_r0 = t->sel_r1 = -1;
            else
                copy_sel(hw);
            InvalidateRect(hw, NULL, FALSE);
        }
        return 0;
    case WM_RBUTTONUP: {
        POINTS p = MAKEPOINTS(lp);
        ctx_menu(hw, p.x, p.y);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_T_COPY: copy_sel(hw); break;
        case IDM_T_PASTE: paste(hw); break;
        case IDM_T_ALL:
            if (t && t->line_count > 0) {
                t->sel_r0 = 0; t->sel_c0 = 0;
                t->sel_r1 = t->line_count - 1;
                t->sel_c1 = t->line_len[t->sel_r1];
                copy_sel(hw);
                InvalidateRect(hw, NULL, FALSE);
            }
            break;
        case IDM_T_CLR: term_clear(hw); break;
        }
        return 0;
    case WM_CHAR:
        if (t && t->sess) {
            if (wp == 13) send_keys(t, "\r", 1);
            else if (wp == 8) send_keys(t, "\x7f", 1);
            else if (wp == 3) send_keys(t, "\x03", 1);
            else if (wp >= 32) {
                char u8[8]; wchar_t w = (wchar_t)wp;
                int n = WideCharToMultiByte(CP_UTF8, 0, &w, 1, u8, 8, NULL, NULL);
                send_keys(t, u8, n);
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (t && t->sess) {
            if (wp == VK_LEFT) send_keys(t, "\x1b[D", 3);
            else if (wp == VK_RIGHT) send_keys(t, "\x1b[C", 3);
            else if (wp == VK_UP) send_keys(t, "\x1b[A", 3);
            else if (wp == VK_DOWN) send_keys(t, "\x1b[B", 3);
            else if (wp == VK_HOME) send_keys(t, "\x1b[H", 3);
            else if (wp == VK_END) send_keys(t, "\x1b[F", 3);
            else if (wp == VK_DELETE) send_keys(t, "\x1b[3~", 4);
            else if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000)) {
                if (t->sel_r0 >= 0) copy_sel(hw); else copy_all(hw);
            } else if (wp == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) paste(hw);
            else if (wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000))
                SendMessageW(hw, WM_COMMAND, IDM_T_ALL, 0);
            else if (wp == 'F' && (GetKeyState(VK_CONTROL) & 0x8000))
                PostMessageW(GetParent(hw), WM_COMMAND, 4100, 0);
        }
        if (wp == VK_PRIOR && t) { t->scroll += 10; InvalidateRect(hw, NULL, FALSE); }
        if (wp == VK_NEXT && t) { t->scroll -= 10; if (t->scroll < 0) t->scroll = 0; InvalidateRect(hw, NULL, FALSE); }
        return 0;
    case WM_MOUSEWHEEL:
        if (t) {
            int d = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 3 : -3;
            t->scroll += d;
            if (t->scroll < 0) t->scroll = 0;
            InvalidateRect(hw, NULL, FALSE);
        }
        return 0;
    case WM_DESTROY:
        if (t) {
            int i;
            KillTimer(hw, IDT_REDRAW);
            KillTimer(hw, IDT_STATUS);
            for (i = 0; i < t->line_count; i++) { free(t->lines[i]); free(t->fg[i]); }
            free(t->lines); free(t->fg); free(t->line_len);
            if (t->font) DeleteObject(t->font);
            if (t->mem_bmp) DeleteObject(t->mem_bmp);
            if (t->mem_dc) DeleteDC(t->mem_dc);
            free(t);
        }
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

HWND term_create(HWND parent, int id, HINSTANCE hi)
{
    static BOOL reg = FALSE;
    if (!reg) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = TermProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(NULL, IDC_IBEAM);
        wc.hbrBackground = CreateSolidBrush(BG);
        wc.lpszClassName = TERM_CLASS;
        wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
        RegisterClassW(&wc);
        reg = TRUE;
    }
    return CreateWindowExW(WS_EX_CLIENTEDGE, TERM_CLASS, L"",
                           WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                           0, 0, 200, 200, parent, (HMENU)(INT_PTR)id, hi, NULL);
}
