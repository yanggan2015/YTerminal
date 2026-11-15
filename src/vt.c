#include "vt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ */
/* 内部结构                                                             */
/* ------------------------------------------------------------------ */

typedef struct ScreenScr {
    int cols, rows;
    Cell *cells;

    int cx, cy;
    int pending_wrap;
    u32 fg, bg;
    u8  attr;
    int style;      /* 0 block, 1 underline, 2 bar */
    int visible;

    int autowrap, origin, insert, lnm;
    int appcursor, appkeypad, bracketpaste, focus;
    int mousemode, mouse_sgr;

    int top, bottom;
    int charsets[2]; /* 0: ASCII, 1: DEC graphics */
    int shift;       /* 0: G0, 1: G1 */

    /* 保存的光标 */
    int saved_valid;
    int saved_x, saved_y;
    u32 saved_fg, saved_bg;
    u8  saved_attr;
    int saved_origin, saved_autowrap, saved_shift;
    int saved_charsets[2];
} ScreenScr;

enum {
    PS_GROUND = 0,
    PS_ESC,
    PS_ESC_INT,
    PS_CSI_ENTRY,
    PS_CSI_PARAM,
    PS_CSI_INT,
    PS_CSI_IGNORE,
    PS_OSC,
    PS_DCS_ENTRY,
    PS_DCS_PARAM,
    PS_DCS_INT,
    PS_DCS_PASSTHROUGH,
    PS_DCS_IGNORE,
    PS_SOS_PM_APC
};

struct VT {
    ScreenScr scr[2];
    int active;

    /* 历史行（环形） */
    Cell *hist;
    int hist_cols;
    int hist_cap, hist_len, hist_first;

    int view_offset;   /* 向上滚动的行数 */

    /* 解析器 */
    int state;
    int params[32];
    int nparams;
    int cur_param;
    int param_digits;
    int private_char;
    int intermediate;
    int csi_leading;   /* 是否已有参数（用于 0 参数语义） */
    StrBuf osc;
    int osc_type;
    int osc_is_dcs;
    int decs_dcs;

    u32 utf8_acc;
    int utf8_need;
    int utf8_seen;

    u32 last_cp;
    int last_width;

    StrBuf title;
    int title_dirty;

    void *user;
    VTWriteFn write_cb;
    VTSimpleFn bell_cb;
    VTTitleFn title_cb;
    VTNotifyFn notify_cb;
    VTClipFn clip_cb;
};

/* DEC 特殊图形字符（0x60..0x7E） */
static const u32 dec_graphics[32] = {
    0x25C6, /* ` ◆ */
    0x2592, /* a ▒ */
    0x2409, /* b ␉ */
    0x240C, /* c ␌ */
    0x240D, /* d ␍ */
    0x240A, /* e ␊ */
    0x00B0, /* f ° */
    0x00B1, /* g ± */
    0x2424, /* h ␤ */
    0x240B, /* i ␋ */
    0x2518, /* j ┘ */
    0x2510, /* k ┐ */
    0x250C, /* l ┌ */
    0x2514, /* m └ */
    0x253C, /* n ┼ */
    0x23BA, /* o ⎺ */
    0x23BB, /* p ⎻ */
    0x2500, /* q ─ */
    0x23BC, /* r ⎼ */
    0x23BD, /* s ⎽ */
    0x251C, /* t ├ */
    0x2524, /* u ┤ */
    0x2534, /* v ┴ */
    0x252C, /* w ┬ */
    0x2502, /* x │ */
    0x2264, /* y ≤ */
    0x2265, /* z ≥ */
    0x03C0, /* { �� */
    0x2260, /* | ≠ */
    0x00A3, /* } £ */
    0x00B7, /* ~ · */
    0x0020
};

/* ------------------------------------------------------------------ */
/* 基础工具                                                             */
/* ------------------------------------------------------------------ */

static void erase_cell(VT *vt, Cell *c) {
    ScreenScr *s = &vt->scr[vt->active];
    c->cp = ' ';
    c->cp2 = 0;
    c->fg = VT_DEFAULT_FG;
    c->bg = s->bg;
    c->attr = 0;
    c->width = 1;
}

static void clear_row(VT *vt, int y, int x0, int x1) {
    ScreenScr *s = &vt->scr[vt->active];
    if (y < 0 || y >= s->rows) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= s->cols) x1 = s->cols - 1;
    for (int x = x0; x <= x1; x++) erase_cell(vt, &s->cells[y * s->cols + x]);
}

static void alloc_screen(ScreenScr *s, int cols, int rows) {
    s->cols = cols;
    s->rows = rows;
    s->cells = (Cell *)xcalloc((size_t)cols * rows, sizeof(Cell));
    for (int i = 0; i < cols * rows; i++) {
        s->cells[i].cp = ' ';
        s->cells[i].width = 1;
        s->cells[i].fg = VT_DEFAULT_FG;
        s->cells[i].bg = VT_DEFAULT_BG;
    }
}

static void screen_reset(ScreenScr *s, int cols, int rows) {
    if (s->cells) free(s->cells);
    s->cells = NULL;
    alloc_screen(s, cols, rows);
    s->cx = s->cy = 0;
    s->pending_wrap = 0;
    s->fg = VT_DEFAULT_FG;
    s->bg = VT_DEFAULT_BG;
    s->attr = 0;
    s->style = 0;
    s->visible = 1;
    s->autowrap = 1;
    s->origin = 0;
    s->insert = 0;
    s->lnm = 0;
    s->appcursor = 0;
    s->appkeypad = 0;
    s->bracketpaste = 0;
    s->focus = 0;
    s->mousemode = 0;
    s->mouse_sgr = 0;
    s->top = 0;
    s->bottom = rows - 1;
    s->charsets[0] = 0; s->charsets[1] = 0;
    s->shift = 0;
    s->saved_valid = 0;
}

/* ------------------------------------------------------------------ */
/* 历史行                                                               */
/* ------------------------------------------------------------------ */

static Cell *hist_line(VT *vt, int i) {
    if (i < 0 || i >= vt->hist_len) return NULL;
    int idx = (vt->hist_first + i) % vt->hist_cap;
    return vt->hist + (size_t)idx * vt->hist_cols;
}

static void hist_push_row_from(VT *vt, int y) {
    if (vt->hist_cap <= 0) return;
    ScreenScr *s = &vt->scr[vt->active];
    if (y < 0 || y >= s->rows) return;
    Cell *line = (Cell *)xmalloc((size_t)vt->hist_cols * sizeof(Cell));
    for (int x = 0; x < vt->hist_cols; x++) {
        if (x < s->cols) line[x] = s->cells[y * s->cols + x];
        else { line[x].cp = ' '; line[x].cp2 = 0; line[x].fg = VT_DEFAULT_FG; line[x].bg = VT_DEFAULT_BG; line[x].attr = 0; line[x].width = 1; }
    }
    if (vt->hist_len == vt->hist_cap) {
        int idx = vt->hist_first;
        memcpy(vt->hist + (size_t)idx * vt->hist_cols, line, (size_t)vt->hist_cols * sizeof(Cell));
        vt->hist_first = (vt->hist_first + 1) % vt->hist_cap;
    } else {
        int idx = (vt->hist_first + vt->hist_len) % vt->hist_cap;
        memcpy(vt->hist + (size_t)idx * vt->hist_cols, line, (size_t)vt->hist_cols * sizeof(Cell));
        vt->hist_len++;
    }
    free(line);
}

void vt_clear_history(VT *vt) {
    vt->hist_len = 0;
    vt->hist_first = 0;
    vt->view_offset = 0;
}

/* ------------------------------------------------------------------ */
/* 滚动                                                                 */
/* ------------------------------------------------------------------ */

static void scroll_up(VT *vt, int n) {
    ScreenScr *s = &vt->scr[vt->active];
    int region = s->bottom - s->top + 1;
    if (n <= 0) return;
    if (n > region) n = region;
    if (s->top == 0 && s->bottom == s->rows - 1 && vt->active == 0) {
        for (int i = 0; i < n; i++) hist_push_row_from(vt, i);
    }
    int move = region - n;
    if (move > 0) {
        memmove(&s->cells[s->top * s->cols], &s->cells[(s->top + n) * s->cols],
                (size_t)move * s->cols * sizeof(Cell));
    }
    for (int y = s->bottom - n + 1; y <= s->bottom; y++) clear_row(vt, y, 0, s->cols - 1);
}

static void scroll_down(VT *vt, int n) {
    ScreenScr *s = &vt->scr[vt->active];
    int region = s->bottom - s->top + 1;
    if (n <= 0) return;
    if (n > region) n = region;
    int move = region - n;
    if (move > 0) {
        memmove(&s->cells[(s->top + n) * s->cols], &s->cells[s->top * s->cols],
                (size_t)move * s->cols * sizeof(Cell));
    }
    for (int y = s->top; y < s->top + n; y++) clear_row(vt, y, 0, s->cols - 1);
}

/* ------------------------------------------------------------------ */
/* 光标/换行                                                            */
/* ------------------------------------------------------------------ */

static void do_linefeed(VT *vt) {
    ScreenScr *s = &vt->scr[vt->active];
    s->pending_wrap = 0;
    if (s->cy == s->bottom) scroll_up(vt, 1);
    else if (s->cy < s->rows - 1) s->cy++;
    if (vt->view_offset) vt->view_offset = 0;
}

static void do_reverse_index(VT *vt) {
    ScreenScr *s = &vt->scr[vt->active];
    if (s->cy == s->top) scroll_down(vt, 1);
    else if (s->cy > 0) s->cy--;
}

static void tab_forward(VT *vt, int n) {
    ScreenScr *s = &vt->scr[vt->active];
    for (int i = 0; i < n; i++) {
        int next = s->cx + 1;
        while (next < s->cols - 1 && ((next % 8) != 0)) next++;
        s->cx = next;
        if (s->cx >= s->cols) s->cx = s->cols - 1;
    }
    s->pending_wrap = 0;
}

static void tab_back(VT *vt, int n) {
    ScreenScr *s = &vt->scr[vt->active];
    for (int i = 0; i < n; i++) {
        int prev = s->cx - 1;
        while (prev > 0 && ((prev % 8) != 0)) prev--;
        s->cx = prev < 0 ? 0 : prev;
    }
    s->pending_wrap = 0;
}

static void cursor_to(VT *vt, int row, int col) {
    ScreenScr *s = &vt->scr[vt->active];
    if (s->origin) {
        row += s->top;
        if (row > s->bottom) row = s->bottom;
        if (row < s->top) row = s->top;
    } else {
        if (row < 0) row = 0;
        if (row > s->rows - 1) row = s->rows - 1;
    }
    if (col < 0) col = 0;
    if (col > s->cols - 1) col = s->cols - 1;
    s->cy = row;
    s->cx = col;
    s->pending_wrap = 0;
}

/* ------------------------------------------------------------------ */
/* 打印                                                                 */
/* ------------------------------------------------------------------ */

static void put_cell(VT *vt, Cell *c, u32 cp) {
    ScreenScr *s = &vt->scr[vt->active];
    c->cp = cp;
    c->cp2 = 0;
    c->fg = s->fg;
    c->bg = s->bg;
    c->attr = s->attr;
    c->width = 1;
}

static void print_cp(VT *vt, u32 cp) {
    ScreenScr *s = &vt->scr[vt->active];
    int width = ucs_width(cp);
    if (width == 0) {
        /* 组合符号：附加到前一格 */
        if (s->cx > 0) {
            Cell *prev = &s->cells[s->cy * s->cols + (s->cx - 1)];
            if (prev->width == 0 && s->cx >= 2) prev = &s->cells[s->cy * s->cols + (s->cx - 2)];
            if (prev->cp2 == 0) prev->cp2 = cp;
        }
        return;
    }
    if (s->pending_wrap) {
        s->cx = 0;
        do_linefeed(vt);
        s->pending_wrap = 0;
    }
    if (width == 2 && s->cx == s->cols - 1) {
        if (s->autowrap) {
            s->cx = 0;
            do_linefeed(vt);
        } else {
            return; /* 无处可放 */
        }
    }
    if (s->insert) {
        int need = width;
        for (int x = s->cols - 1; x >= s->cx + need; x--)
            s->cells[s->cy * s->cols + x] = s->cells[s->cy * s->cols + x - need];
        for (int i = 0; i < need; i++) erase_cell(vt, &s->cells[s->cy * s->cols + s->cx + i]);
    }
    Cell *c = &s->cells[s->cy * s->cols + s->cx];
    put_cell(vt, c, cp);
    c->width = (u8)width;
    if (width == 2) {
        Cell *c2 = &s->cells[s->cy * s->cols + s->cx + 1];
        c2->cp = 0;
        c2->cp2 = 0;
        c2->fg = s->fg;
        c2->bg = s->bg;
        c2->attr = s->attr;
        c2->width = 0;
    }
    s->cx += width;
    if (s->cx >= s->cols) {
        s->cx = s->cols - 1;
        s->pending_wrap = 1;
    }
    vt->last_cp = cp;
    vt->last_width = width;
    if (vt->view_offset) vt->view_offset = 0;
}

/* ------------------------------------------------------------------ */
/* SGR                                                                  */
/* ------------------------------------------------------------------ */

static u32 sgr_index_color(int n, int bright) {
    if (bright) n += 8;
    return (u32)((n < 0 ? 0 : n) & 0xFF);
}

static void sgr_reset(VT *vt) {
    ScreenScr *s = &vt->scr[vt->active];
    s->fg = VT_DEFAULT_FG;
    s->bg = VT_DEFAULT_BG;
    s->attr = 0;
}

static void apply_sgr(VT *vt) {
    ScreenScr *s = &vt->scr[vt->active];
    if (vt->nparams == 0) { sgr_reset(vt); return; }
    for (int i = 0; i < vt->nparams; i++) {
        int n = vt->params[i];
        if (n == 0) { sgr_reset(vt); continue; }
        if (n == 1) s->attr |= ATTR_BOLD;
        else if (n == 2) s->attr |= ATTR_DIM;
        else if (n == 3) s->attr |= ATTR_ITALIC;
        else if (n == 4) s->attr |= ATTR_UNDERLINE;
        else if (n == 5 || n == 6) s->attr |= ATTR_BLINK;
        else if (n == 7) s->attr |= ATTR_INVERSE;
        else if (n == 8) s->attr |= ATTR_HIDDEN;
        else if (n == 9) s->attr |= ATTR_STRIKE;
        else if (n == 21 || n == 22) s->attr &= (u8)~(ATTR_BOLD | ATTR_DIM);
        else if (n == 23) s->attr &= (u8)~ATTR_ITALIC;
        else if (n == 24) s->attr &= (u8)~ATTR_UNDERLINE;
        else if (n == 25) s->attr &= (u8)~ATTR_BLINK;
        else if (n == 27) s->attr &= (u8)~ATTR_INVERSE;
        else if (n == 28) s->attr &= (u8)~ATTR_HIDDEN;
        else if (n == 29) s->attr &= (u8)~ATTR_STRIKE;
        else if (n >= 30 && n <= 37) s->fg = sgr_index_color(n - 30, 0);
        else if (n == 38) {
            if (i + 2 < vt->nparams && vt->params[i + 1] == 5) {
                s->fg = (u32)(vt->params[i + 2] & 0xFF); i += 2;
            } else if (i + 4 < vt->nparams && vt->params[i + 1] == 2) {
                s->fg = VT_RGB(vt->params[i + 2] & 0xFF, vt->params[i + 3] & 0xFF, vt->params[i + 4] & 0xFF); i += 4;
            }
        } else if (n == 39) s->fg = VT_DEFAULT_FG;
        else if (n >= 40 && n <= 47) s->bg = sgr_index_color(n - 40, 0);
        else if (n == 48) {
            if (i + 2 < vt->nparams && vt->params[i + 1] == 5) {
                s->bg = (u32)(vt->params[i + 2] & 0xFF); i += 2;
            } else if (i + 4 < vt->nparams && vt->params[i + 1] == 2) {
                s->bg = VT_RGB(vt->params[i + 2] & 0xFF, vt->params[i + 3] & 0xFF, vt->params[i + 4] & 0xFF); i += 4;
            }
        } else if (n == 49) s->bg = VT_DEFAULT_BG;
        else if (n >= 90 && n <= 97) s->fg = sgr_index_color(n - 90, 1);
        else if (n >= 100 && n <= 107) s->bg = sgr_index_color(n - 100, 1);
        /* 58/59 下划线颜色忽略 */
    }
}

/* ------------------------------------------------------------------ */
/* 模式切换                                                             */
/* ------------------------------------------------------------------ */

static void switch_screen(VT *vt, int to_alt, int save_cursor) {
    ScreenScr *s = &vt->scr[vt->active];
    if (save_cursor) {
        s->saved_valid = 1;
        s->saved_x = s->cx; s->saved_y = s->cy;
        s->saved_fg = s->fg; s->saved_bg = s->bg; s->saved_attr = s->attr;
        s->saved_origin = s->origin; s->saved_autowrap = s->autowrap;
        s->saved_shift = s->shift;
        s->saved_charsets[0] = s->charsets[0];
        s->saved_charsets[1] = s->charsets[1];
    }
    if (to_alt) {
        if (vt->active == 0) {
            vt->active = 1;
            ScreenScr *a = &vt->scr[1];
            for (int y = 0; y < a->rows; y++) clear_row(vt, y, 0, a->cols - 1);
            a->cx = a->cy = 0;
            a->pending_wrap = 0;
            a->top = 0; a->bottom = a->rows - 1;
            a->fg = s->fg; a->bg = s->bg; a->attr = s->attr;
            a->visible = 1;
            a->mousemode = 0; a->mouse_sgr = 0;
            a->bracketpaste = s->bracketpaste;
            a->appcursor = 0;
        }
    } else {
        if (vt->active == 1) {
            vt->active = 0;
            ScreenScr *m = &vt->scr[0];
            if (m->saved_valid) {
                m->cx = m->saved_x; m->cy = m->saved_y;
                m->fg = m->saved_fg; m->bg = m->saved_bg; m->attr = m->saved_attr;
                m->origin = m->saved_origin;
                m->autowrap = m->saved_autowrap;
                m->shift = m->saved_shift;
                m->charsets[0] = m->saved_charsets[0];
                m->charsets[1] = m->saved_charsets[1];
            }
        }
    }
    vt->view_offset = 0;
}

static void set_mode(VT *vt, int private_mode, int mode, int on) {
    ScreenScr *s = &vt->scr[vt->active];
    if (!private_mode) {
        if (mode == 4) s->insert = on;          /* IRM */
        else if (mode == 20) s->lnm = on;       /* LNM */
        return;
    }
    switch (mode) {
    case 1: s->appcursor = on; break;
    case 6: s->origin = on; s->cx = 0; s->cy = s->origin ? s->top : 0; break;
    case 7: s->autowrap = on; if (!on) s->pending_wrap = 0; break;
    case 12: break; /* 光标闪烁 */
    case 25: s->visible = on; break;
    case 47: switch_screen(vt, on, 0); break;
    case 1047: switch_screen(vt, on, 0); break;
    case 1048:
        s->saved_valid = 1;
        s->saved_x = s->cx; s->saved_y = s->cy;
        s->saved_fg = s->fg; s->saved_bg = s->bg; s->saved_attr = s->attr;
        break;
    case 1049: switch_screen(vt, on, 1); break;
    case 1000: s->mousemode = on ? 1 : 0; break;
    case 1001: break;
    case 1002: s->mousemode = on ? 2 : 0; break;
    case 1003: s->mousemode = on ? 3 : 0; break;
    case 1004: s->focus = on; break;
    case 1005: break;            /* UTF-8 鼠标仅 xterm */
    case 1006: s->mouse_sgr = on; break;
    case 1015: s->mouse_sgr = on ? 2 : 0; break;
    case 2004: s->bracketpaste = on; break;
    case 3: break;  /* DECCOLM 忽略 */
    case 5: break;  /* 反显整屏由渲染层处理（忽略） */
    case 9001: break;
    default: break;
    }
}

/* ------------------------------------------------------------------ */
/* 应答                                                                 */
/* ------------------------------------------------------------------ */

static void reply(VT *vt, const char *s, size_t n) {
    if (vt->write_cb) vt->write_cb(vt->user, s, n);
}
static void replyf(VT *vt, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n > 0) reply(vt, buf, (size_t)n);
}

static void set_title_internal(VT *vt, const char *t) {
    free(vt->title.buf);
    sb_init(&vt->title);
    sb_append(&vt->title, t);
    vt->title_dirty = 1;
    if (vt->title_cb) vt->title_cb(vt->user, t);
}

/* OSC 52 剪贴板（base64） */
static void osc52_set(VT *vt, const char *payload) {
    /* payload 形如 "c;<base64>" 或 "p;<base64>" */
    const char *semi = strchr(payload, ';');
    if (!semi) return;
    const char *b64 = semi + 1;
    size_t n = strlen(b64);
    if (n == 0) return;
    unsigned char *out = (unsigned char *)xmalloc(n + 1);
    size_t outn = 0;
    int v = 0, bits = 0;
    for (size_t i = 0; i < n; i++) {
        char c = b64[i];
        int d;
        if (c >= 'A' && c <= 'Z') d = c - 'A';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
        else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '+') d = 62;
        else if (c == '/') d = 63;
        else continue; /* '=' 或空白 */
        v = (v << 6) | d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[outn++] = (unsigned char)((v >> bits) & 0xFF);
        }
    }
    out[outn] = 0;
    if (vt->clip_cb && outn) vt->clip_cb(vt->user, (const char *)out);
    free(out);
}

/* ------------------------------------------------------------------ */
/* OSC / DCS 完成                                                       */
/* ------------------------------------------------------------------ */

static void osc_finish(VT *vt) {
    if (vt->osc.len == 0) return;
    char *s = vt->osc.buf;
    char *semi = strchr(s, ';');
    int code = atoi(s);
    char *body = semi ? semi + 1 : s;
    if (code == 0 || code == 1 || code == 2) {
        set_title_internal(vt, body);
    } else if (code == 9) {
        char *semi2 = strchr(body, ';');
        if (semi2) {
            *semi2 = 0;
            if (vt->notify_cb) vt->notify_cb(vt->user, body, semi2 + 1);
            *semi2 = ';';
        }
    } else if (code == 7) {
        /* 当前工作目录 */
    } else if (code == 52) {
        osc52_set(vt, body);
    } else if (code == 4) {
        /* 调色板设置：忽略 */
    }
}

/* ------------------------------------------------------------------ */
/* CSI 处理                                                             */
/* ------------------------------------------------------------------ */

static int pget(VT *vt, int i, int def) {
    if (i >= vt->nparams) return def;
    return vt->params[i] < 0 ? 0 : vt->params[i];
}

static void csi_dispatch(VT *vt, char final_ch) {
    ScreenScr *s = &vt->scr[vt->active];
    int n;
    char priv = (char)vt->private_char;
    switch (final_ch) {
    case 'A': n = pget(vt, 0, 1); if (n < 1) n = 1; s->cy -= n; if (s->cy < (s->origin ? s->top : 0)) s->cy = s->origin ? s->top : 0; s->pending_wrap = 0; break;
    case 'B': n = pget(vt, 0, 1); if (n < 1) n = 1; s->cy += n; if (s->cy > (s->origin ? s->bottom : s->rows - 1)) s->cy = s->origin ? s->bottom : s->rows - 1; s->pending_wrap = 0; break;
    case 'C': n = pget(vt, 0, 1); if (n < 1) n = 1; s->cx += n; if (s->cx >= s->cols) s->cx = s->cols - 1; s->pending_wrap = 0; break;
    case 'D': n = pget(vt, 0, 1); if (n < 1) n = 1; s->cx -= n; if (s->cx < 0) s->cx = 0; s->pending_wrap = 0; break;
    case 'E': n = pget(vt, 0, 1); if (n < 1) n = 1; s->cx = 0; s->cy += n; if (s->cy > s->rows - 1) s->cy = s->rows - 1; s->pending_wrap = 0; break;
    case 'F': n = pget(vt, 0, 1); if (n < 1) n = 1; s->cx = 0; s->cy -= n; if (s->cy < 0) s->cy = 0; s->pending_wrap = 0; break;
    case 'G': case '`': n = pget(vt, 0, 1); s->cx = n - 1; if (s->cx < 0) s->cx = 0; if (s->cx >= s->cols) s->cx = s->cols - 1; s->pending_wrap = 0; break;
    case 'H': case 'f': {
        int row = pget(vt, 0, 1), col = pget(vt, 1, 1);
        cursor_to(vt, row - 1, col - 1); break;
    }
    case 'd': n = pget(vt, 0, 1); cursor_to(vt, n - 1, s->cx); break;
    case 'e': n = pget(vt, 0, 1); if (n < 1) n = 1; cursor_to(vt, s->cy + n, s->cx); break;
    case 'a': n = pget(vt, 0, 1); if (n < 1) n = 1; cursor_to(vt, s->cy, s->cx + n); break;
    case 'I': n = pget(vt, 0, 1); if (n < 1) n = 1; tab_forward(vt, n); break;
    case 'Z': n = pget(vt, 0, 1); if (n < 1) n = 1; tab_back(vt, n); break;
    case 'J': {
        int mode = pget(vt, 0, 0);
        if (mode == 0) {
            clear_row(vt, s->cy, s->cx, s->cols - 1);
            for (int y = s->cy + 1; y < s->rows; y++) clear_row(vt, y, 0, s->cols - 1);
        } else if (mode == 1) {
            for (int y = 0; y < s->cy; y++) clear_row(vt, y, 0, s->cols - 1);
            clear_row(vt, s->cy, 0, s->cx);
        } else if (mode == 2) {
            for (int y = 0; y < s->rows; y++) clear_row(vt, y, 0, s->cols - 1);
        } else if (mode == 3) {
            for (int y = 0; y < s->rows; y++) clear_row(vt, y, 0, s->cols - 1);
            vt_clear_history(vt);
        }
        break;
    }
    case 'K': {
        int mode = pget(vt, 0, 0);
        if (mode == 0) clear_row(vt, s->cy, s->cx, s->cols - 1);
        else if (mode == 1) clear_row(vt, s->cy, 0, s->cx);
        else if (mode == 2) clear_row(vt, s->cy, 0, s->cols - 1);
        break;
    }
    case 'X': n = pget(vt, 0, 1); if (n < 1) n = 1; clear_row(vt, s->cy, s->cx, s->cx + n - 1); break;
    case 'L': {
        n = pget(vt, 0, 1); if (n < 1) n = 1;
        if (s->cy < s->top || s->cy > s->bottom) break;
        int region = s->bottom - s->cy + 1;
        if (n > region) n = region;
        memmove(&s->cells[(s->cy + n) * s->cols], &s->cells[s->cy * s->cols],
                (size_t)(region - n) * s->cols * sizeof(Cell));
        for (int y = s->cy; y < s->cy + n; y++) clear_row(vt, y, 0, s->cols - 1);
        s->cx = 0; s->pending_wrap = 0;
        break;
    }
    case 'M': {
        n = pget(vt, 0, 1); if (n < 1) n = 1;
        if (s->cy < s->top || s->cy > s->bottom) break;
        int region = s->bottom - s->cy + 1;
        if (n > region) n = region;
        if (region - n > 0)
            memmove(&s->cells[s->cy * s->cols], &s->cells[(s->cy + n) * s->cols],
                    (size_t)(region - n) * s->cols * sizeof(Cell));
        for (int y = s->bottom - n + 1; y <= s->bottom; y++) clear_row(vt, y, 0, s->cols - 1);
        s->cx = 0; s->pending_wrap = 0;
        break;
    }
    case 'P': {
        n = pget(vt, 0, 1); if (n < 1) n = 1;
        if (n > s->cols - s->cx) n = s->cols - s->cx;
        int row = s->cy * s->cols;
        memmove(&s->cells[row + s->cx], &s->cells[row + s->cx + n],
                (size_t)(s->cols - s->cx - n) * sizeof(Cell));
        for (int x = s->cols - n; x < s->cols; x++) erase_cell(vt, &s->cells[row + x]);
        break;
    }
    case '@': {
        n = pget(vt, 0, 1); if (n < 1) n = 1;
        if (n > s->cols - s->cx) n = s->cols - s->cx;
        int row = s->cy * s->cols;
        memmove(&s->cells[row + s->cx + n], &s->cells[row + s->cx],
                (size_t)(s->cols - s->cx - n) * sizeof(Cell));
        for (int x = s->cx; x < s->cx + n; x++) erase_cell(vt, &s->cells[row + x]);
        break;
    }
    case 'S': n = pget(vt, 0, 1); if (n < 1) n = 1; scroll_up(vt, n); break;
    case 'T': n = pget(vt, 0, 1); if (n < 1) n = 1; scroll_down(vt, n); break;
    case 'b': {
        if (vt->last_cp) {
            int cnt = pget(vt, 0, 1);
            for (int i = 0; i < cnt; i++) print_cp(vt, vt->last_cp);
        }
        break;
    }
    case 'm': apply_sgr(vt); break;
    case 'r': {
        int top = pget(vt, 0, 1) - 1;
        int bottom = pget(vt, 1, s->rows) - 1;
        if (bottom > s->rows - 1) bottom = s->rows - 1;
        if (top < 0) top = 0;
        if (bottom > top) {
            s->top = top; s->bottom = bottom;
            cursor_to(vt, 0, 0);
            if (s->origin) s->cy = s->top;
        }
        break;
    }
    case 's':
        s->saved_valid = 1;
        s->saved_x = s->cx; s->saved_y = s->cy;
        s->saved_fg = s->fg; s->saved_bg = s->bg; s->saved_attr = s->attr;
        break;
    case 'u':
        if (s->saved_valid) {
            s->cx = s->saved_x; s->cy = s->saved_y;
            s->fg = s->saved_fg; s->bg = s->saved_bg; s->attr = s->saved_attr;
        }
        break;
    case 'h': case 'l': {
        int on = (final_ch == 'h');
        if (vt->nparams == 0) set_mode(vt, priv == '?', 0, on);
        for (int i = 0; i < vt->nparams; i++) {
            int m = vt->params[i];
            if (m < 0) continue;
            set_mode(vt, priv == '?', m, on);
        }
        break;
    }
    case 'n': {
        int mode = pget(vt, 0, 0);
        if (mode == 5) reply(vt, "\x1b[0n", 4);
        else if (mode == 6) replyf(vt, "\x1b[%d;%dR", s->cy + 1, s->cx + 1);
        break;
    }
    case 'c': {
        if (priv == '>') reply(vt, "\x1b[>0;276;0c", 11);
        else reply(vt, "\x1b[?62;22;52c", 12);
        break;
    }
    case 't': {
        int op = pget(vt, 0, 0);
        if (op == 18) replyf(vt, "\x1b[8;%d;%dt", s->rows, s->cols);
        break;
    }
    case 'q': {
        n = pget(vt, 0, 0);
        if (n <= 0) s->style = 0;
        else if (n == 1 || n == 2) s->style = 0;
        else if (n == 3 || n == 4) s->style = 1;
        else s->style = 2;
        break;
    }
    case 'g': {
        n = pget(vt, 0, 0);
        (void)n; /* 制表位固定 8 列，忽略 */
        break;
    }
    default: break;
    }
}

/* ------------------------------------------------------------------ */
/* 主解析                                                               */
/* ------------------------------------------------------------------ */

static void osc_begin(VT *vt) {
    sb_clear(&vt->osc);
    vt->osc_type = 0;
    

}
static void osc_append_cp(VT *vt, u32 cp) {
    char tmp[8];
    size_t n = utf8_encode_one(cp, tmp);
    sb_append_n(&vt->osc, tmp, n);
}

static void handle_c0(VT *vt, u32 cp) {
    ScreenScr *s = &vt->scr[vt->active];
    switch (cp) {
    case 0x07: if (vt->bell_cb) vt->bell_cb(vt->user); break;
    case 0x08: if (s->cx > 0) s->cx--; s->pending_wrap = 0; break;
    case 0x09: tab_forward(vt, 1); break;
    case 0x0A: case 0x0B: case 0x0C: do_linefeed(vt); if (s->lnm) s->cx = 0; break;
    case 0x0D: s->cx = 0; s->pending_wrap = 0; break;
    case 0x0E: s->shift = 1; break;
    case 0x0F: s->shift = 0; break;
    default: break;
    }
}

static void esc_dispatch(VT *vt, u32 cp, int intermediate) {
    ScreenScr *s = &vt->scr[vt->active];
    if (intermediate) {
        if (intermediate == '(' || intermediate == ')' || intermediate == '*' || intermediate == '+') {
            int slot = (intermediate == '(' || intermediate == '*') ? 0 : 1;
            s->charsets[slot] = (cp == '0') ? 1 : 0;
        }
        if (intermediate == '#') {
            if (cp == '8') {
                for (int y = 0; y < s->rows; y++)
                    for (int x = 0; x < s->cols; x++) put_cell(vt, &s->cells[y * s->cols + x], 'E');
            }
        }
        return;
    }
    switch (cp) {
    case '7':
        s->saved_valid = 1;
        s->saved_x = s->cx; s->saved_y = s->cy;
        s->saved_fg = s->fg; s->saved_bg = s->bg; s->saved_attr = s->attr;
        s->saved_origin = s->origin; s->saved_autowrap = s->autowrap;
        s->saved_shift = s->shift;
        s->saved_charsets[0] = s->charsets[0]; s->saved_charsets[1] = s->charsets[1];
        break;
    case '8':
        if (s->saved_valid) {
            s->cx = s->saved_x; s->cy = s->saved_y;
            s->fg = s->saved_fg; s->bg = s->saved_bg; s->attr = s->saved_attr;
            s->origin = s->saved_origin; s->autowrap = s->saved_autowrap;
            s->shift = s->saved_shift;
            s->charsets[0] = s->saved_charsets[0]; s->charsets[1] = s->saved_charsets[1];
            s->pending_wrap = 0;
        }
        break;
    case 'D': do_linefeed(vt); break;
    case 'E': s->cx = 0; do_linefeed(vt); break;
    case 'H': break;
    case 'M': do_reverse_index(vt); break;
    case 'c': vt_reset(vt); break;
    case '=': s->appkeypad = 1; break;
    case '>': s->appkeypad = 0; break;
    case '\\': break; /* ST */
    case 'N': break;
    case 'O': break;
    case 'Z': reply(vt, "\x1b[?62c", 6); break;
    default: break;
    }
}

void vt_feed(VT *vt, const u8 *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        u32 b = data[i];
        u32 cp = 0;
        if (vt->utf8_need) {
            if ((b & 0xC0) == 0x80) {
                vt->utf8_acc = (vt->utf8_acc << 6) | (b & 0x3F);
                vt->utf8_seen++;
                if (vt->utf8_seen >= vt->utf8_need) {
                    cp = vt->utf8_acc;
                    vt->utf8_need = 0; vt->utf8_seen = 0; vt->utf8_acc = 0;
                } else continue;
            } else {
                vt->utf8_need = 0; vt->utf8_seen = 0; vt->utf8_acc = 0;
                continue;
            }
        } else if (b >= 0x80) {
            if ((b & 0xE0) == 0xC0) { vt->utf8_acc = b & 0x1F; vt->utf8_need = 1; vt->utf8_seen = 0; }
            else if ((b & 0xF0) == 0xE0) { vt->utf8_acc = b & 0x0F; vt->utf8_need = 2; vt->utf8_seen = 0; }
            else if ((b & 0xF8) == 0xF0) { vt->utf8_acc = b & 0x07; vt->utf8_need = 3; vt->utf8_seen = 0; }
            continue;
        } else {
            cp = b;
        }

        switch (vt->state) {
        case PS_GROUND:
            if (cp == 0x1B) { vt->state = PS_ESC; vt->intermediate = 0; }
            else if (cp < 0x20) handle_c0(vt, cp);
            else if (cp == 0x7F) { /* DEL 忽略 */ }
            else {
                ScreenScr *s = &vt->scr[vt->active];
                u32 out = cp;
                if (s->charsets[s->shift] == 1 && cp >= 0x60 && cp <= 0x7E) out = dec_graphics[cp - 0x60];
                print_cp(vt, out);
            }
            break;
        case PS_ESC:
            if (cp == '[') {
                vt->state = PS_CSI_ENTRY;
                vt->nparams = 0; vt->cur_param = 0; vt->param_digits = 0;
                vt->private_char = 0; vt->intermediate = 0; vt->csi_leading = 0;
            } else if (cp == ']') {
                vt->state = PS_OSC; osc_begin(vt);
            } else if (cp == 'P') {
                vt->state = PS_DCS_ENTRY; vt->decs_dcs = 1;
            } else if (cp == 'X' || cp == '^' || cp == '_') {
                vt->state = PS_SOS_PM_APC;
            } else if (cp >= 0x20 && cp <= 0x2F) {
                vt->intermediate = (int)cp;
                vt->state = PS_ESC_INT;
            } else if (cp < 0x20) {
                handle_c0(vt, cp);
            } else {
                esc_dispatch(vt, cp, 0);
                vt->state = PS_GROUND;
            }
            break;
        case PS_ESC_INT:
            if (cp >= 0x20 && cp <= 0x2F) {
                vt->intermediate = (int)(vt->intermediate * 100 + cp);
            } else if (cp < 0x20) {
                handle_c0(vt, cp);
            } else {
                /* 需要奇偶判断，这里直接把 intermediate 解析为字符 */
                int inter = vt->intermediate;
                if (inter >= 100) inter = (int)(inter % 100);
                else inter = vt->intermediate;
                esc_dispatch(vt, cp, inter);
                vt->state = PS_GROUND;
            }
            break;
        case PS_CSI_ENTRY:
        case PS_CSI_PARAM:
            if (cp >= '0' && cp <= '9') {
                vt->cur_param = vt->cur_param * 10 + (int)(cp - '0');
                vt->param_digits = 1;
                vt->state = PS_CSI_PARAM;
            } else if (cp == ';' || cp == ':') {
                if (vt->nparams < 31) vt->params[vt->nparams++] = vt->param_digits ? vt->cur_param : -1;
                vt->cur_param = 0; vt->param_digits = 0;
                vt->state = PS_CSI_PARAM;
            } else if (cp == '<' || cp == '=' || cp == '>' || cp == '?') {
                vt->private_char = (int)cp;
                vt->state = PS_CSI_PARAM;
            } else if (cp >= 0x20 && cp <= 0x2F) {
                vt->intermediate = (int)cp;
                vt->state = PS_CSI_INT;
            } else if (cp >= 0x40 && cp <= 0x7E) {
                if (vt->param_digits && vt->nparams < 31) vt->params[vt->nparams++] = vt->cur_param;
                csi_dispatch(vt, (char)cp);
                vt->state = PS_GROUND;
            } else if (cp < 0x20) {
                handle_c0(vt, cp);
            } else {
                vt->state = PS_CSI_IGNORE;
            }
            break;
        case PS_CSI_INT:
            if (cp >= 0x20 && cp <= 0x2F) { /* 继续中间字节 */ }
            else if (cp >= 0x40 && cp <= 0x7E) {
                /* 带中间字节的 CSI 多数不常见，忽略执行 */
                if (vt->param_digits && vt->nparams < 31) vt->params[vt->nparams++] = vt->cur_param;
                csi_dispatch(vt, (char)cp);
                vt->state = PS_GROUND;
            } else if (cp < 0x20) handle_c0(vt, cp);
            else vt->state = PS_CSI_IGNORE;
            break;
        case PS_CSI_IGNORE:
            if (cp >= 0x40 && cp <= 0x7E) vt->state = PS_GROUND;
            else if (cp == 0x1B) vt->state = PS_ESC;
            break;
        case PS_OSC:
            if (cp == 0x07) {
                vt->state = PS_GROUND;
                osc_finish(vt);
            } else if (cp == 0x1B) {
                vt->state = PS_ESC; /* 期待 '\' */
                vt->private_char = 1; /* 标记 ST */
                osc_finish(vt);
            } else if (cp < 0x20 && cp != 0x09) {
                /* 忽略其他控制 */
            } else {
                osc_append_cp(vt, cp);
                if (vt->osc.len > 4094) { vt->state = PS_GROUND; osc_finish(vt); }
            }
            break;
        case PS_DCS_ENTRY:
        case PS_DCS_PARAM:
            if (cp >= 0x40 && cp <= 0x7E) vt->state = PS_DCS_PASSTHROUGH;
            else if (cp >= '0' && cp <= '9') vt->state = PS_DCS_PARAM;
            else if (cp >= 0x20 && cp <= 0x2F) vt->state = PS_DCS_INT;
            else if (cp == 0x1B) vt->state = PS_ESC;
            break;
        case PS_DCS_INT:
            if (cp >= 0x40 && cp <= 0x7E) vt->state = PS_DCS_PASSTHROUGH;
            else if (cp == 0x1B) vt->state = PS_ESC;
            break;
        case PS_DCS_PASSTHROUGH:
            if (cp == 0x1B) vt->state = PS_ESC;
            break;
        case PS_SOS_PM_APC:
            if (cp == 0x1B) vt->state = PS_ESC;
            break;
        default:
            vt->state = PS_GROUND;
            break;
        }
        /* OSC 的 BEL 终止需要区分 ESC \ 后续 */
        if (vt->state == PS_ESC && vt->private_char == 1) {
            /* 已结束 OSC，等待 '\' */
            vt->private_char = 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 生命周期                                                             */
/* ------------------------------------------------------------------ */

VT *vt_create(int cols, int rows, int scrollback) {
    if (cols < 2) cols = 2;
    if (rows < 2) rows = 2;
    VT *vt = (VT *)xcalloc(1, sizeof(VT));
    vt->state = PS_GROUND;
    vt->hist_cap = scrollback > 0 ? scrollback : 0;
    vt->hist_cols = cols;
    if (vt->hist_cap) vt->hist = (Cell *)xcalloc((size_t)vt->hist_cap * cols, sizeof(Cell));
    sb_init(&vt->osc);
    sb_init(&vt->title);
    screen_reset(&vt->scr[0], cols, rows);
    screen_reset(&vt->scr[1], cols, rows);
    vt->active = 0;
    vt->hist_len = 0;
    vt->hist_first = 0;
    return vt;
}

void vt_destroy(VT *vt) {
    if (!vt) return;
    free(vt->scr[0].cells);
    free(vt->scr[1].cells);
    free(vt->hist);
    sb_free(&vt->osc);
    sb_free(&vt->title);
    free(vt);
}

void vt_reset(VT *vt) {
    screen_reset(&vt->scr[0], vt->scr[0].cols, vt->scr[0].rows);
    screen_reset(&vt->scr[1], vt->scr[1].cols, vt->scr[1].rows);
    vt->active = 0;
    vt->view_offset = 0;
    vt->state = PS_GROUND;
}

void vt_resize(VT *vt, int cols, int rows) {
    if (cols < 2) cols = 2;
    if (rows < 2) rows = 2;
    if (cols == vt->scr[0].cols && rows == vt->scr[0].rows) return;

    /* 保存历史并在新宽度下重建 */
    int old_hist_cols = vt->hist_cols;
    int new_hist_cols = cols;
    Cell *new_hist = NULL;
    if (vt->hist_cap) new_hist = (Cell *)xcalloc((size_t)vt->hist_cap * new_hist_cols, sizeof(Cell));
    for (int i = 0; i < vt->hist_cap * new_hist_cols; i++) {
        new_hist[i].cp = ' ';
        new_hist[i].width = 1;
        new_hist[i].fg = VT_DEFAULT_FG;
        new_hist[i].bg = VT_DEFAULT_BG;
    }
    int keep = vt->hist_len;
    if (keep > vt->hist_cap) keep = vt->hist_cap;
    for (int i = 0; i < keep; i++) {
        Cell *src = vt->hist + (size_t)((vt->hist_first + i) % (vt->hist_cap ? vt->hist_cap : 1)) * old_hist_cols;
        Cell *dst = new_hist + (size_t)i * new_hist_cols;
        int n = old_hist_cols < new_hist_cols ? old_hist_cols : new_hist_cols;
        memcpy(dst, src, (size_t)n * sizeof(Cell));
        for (int x = n; x < new_hist_cols; x++) { dst[x].cp = ' '; dst[x].width = 1; dst[x].fg = VT_DEFAULT_FG; dst[x].bg = VT_DEFAULT_BG; }
    }
    free(vt->hist);
    vt->hist = new_hist;
    vt->hist_cols = new_hist_cols;
    vt->hist_len = keep;
    vt->hist_first = 0;

    for (int k = 0; k < 2; k++) {
        ScreenScr *s = &vt->scr[k];
        int oldc = s->cols, oldr = s->rows;
        Cell *old = s->cells;
        s->cells = (Cell *)xcalloc((size_t)cols * rows, sizeof(Cell));
        for (int i = 0; i < cols * rows; i++) {
            s->cells[i].cp = ' ';
            s->cells[i].width = 1;
            s->cells[i].fg = VT_DEFAULT_FG;
            s->cells[i].bg = VT_DEFAULT_BG;
        }
        int cmin = cols < oldc ? cols : oldc;
        int rmin = rows < oldr ? rows : oldr;
        for (int y = 0; y < rmin; y++) {
            memcpy(&s->cells[y * cols], &old[y * oldc], (size_t)cmin * sizeof(Cell));
        }
        free(old);
        s->cols = cols; s->rows = rows;
        if (s->cx >= cols) s->cx = cols - 1;
        if (s->cy >= rows) s->cy = rows - 1;
        s->top = 0; s->bottom = rows - 1;
        s->pending_wrap = 0;
    }
    vt->view_offset = 0;
}

/* ------------------------------------------------------------------ */
/* 查询接口                                                             */
/* ------------------------------------------------------------------ */

int vt_cols(VT *vt) { return vt->scr[vt->active].cols; }
int vt_rows(VT *vt) { return vt->scr[vt->active].rows; }
int vt_history_len(VT *vt) { return vt->hist_len; }
int vt_total_lines(VT *vt) { return vt->hist_len + vt->scr[vt->active].rows; }
int vt_view_offset(VT *vt) { return vt->view_offset; }
int vt_cursor_x(VT *vt) { return vt->scr[vt->active].cx; }
int vt_cursor_y(VT *vt) { return vt->scr[vt->active].cy; }
int vt_cursor_visible(VT *vt) { return vt->scr[vt->active].visible; }
int vt_cursor_style(VT *vt) { return vt->scr[vt->active].style; }
int vt_is_alt(VT *vt) { return vt->active == 1; }
int vt_bracket_paste(VT *vt) { return vt->scr[vt->active].bracketpaste; }
int vt_app_cursor(VT *vt) { return vt->scr[vt->active].appcursor; }
int vt_mouse_mode(VT *vt) { return vt->scr[vt->active].mousemode; }
int vt_mouse_sgr(VT *vt) { return vt->scr[vt->active].mouse_sgr; }
int vt_focus_events(VT *vt) { return vt->scr[vt->active].focus; }
u32 vt_cursor_fg(VT *vt) { return vt->scr[vt->active].fg; }
u32 vt_cursor_bg(VT *vt) { return vt->scr[vt->active].bg; }

void vt_scroll(VT *vt, int delta) {
    vt->view_offset += delta;
    if (vt->view_offset < 0) vt->view_offset = 0;
    if (vt->view_offset > vt->hist_len) vt->view_offset = vt->hist_len;
}
void vt_scroll_to_bottom(VT *vt) { vt->view_offset = 0; }

Cell *vt_line(VT *vt, int index) {
    int rows = vt->scr[vt->active].rows;
    int total = vt->hist_len + rows;
    if (index < 0 || index >= total) return NULL;
    if (index < vt->hist_len) return hist_line(vt, index);
    return &vt->scr[vt->active].cells[(size_t)(index - vt->hist_len) * vt->scr[vt->active].cols];
}

int vt_line_index_for_row(VT *vt, int row) {
    int idx = vt->hist_len - vt->view_offset + row;
    int total = vt->hist_len + vt->scr[vt->active].rows;
    if (idx < 0) idx = 0;
    if (idx >= total) idx = total - 1;
    return idx;
}

char *vt_line_text(VT *vt, int index) {
    Cell *line = vt_line(vt, index);
    if (!line) return xstrdup("");
    int cols = vt->scr[vt->active].cols;
    StrBuf sb; sb_init(&sb);
    int last_non_space = -1;
    for (int x = 0; x < cols; x++) {
        Cell *c = &line[x];
        if (c->width == 0) continue;
        char tmp[8];
        size_t n = utf8_encode_one(c->cp ? c->cp : ' ', tmp);
        sb_append_n(&sb, tmp, n);
        if (c->cp2) {
            size_t n2 = utf8_encode_one(c->cp2, tmp);
            sb_append_n(&sb, tmp, n2);
        }
        if (c->cp != ' ' || c->cp2) last_non_space = (int)sb.len;
    }
    char *s = sb.buf;
    if (last_non_space < 0) s[0] = 0;
    else if ((size_t)last_non_space < sb.len) s[last_non_space] = 0;
    return s;
}

char *vt_screen_text(VT *vt) {
    StrBuf sb; sb_init(&sb);
    int rows = vt->scr[vt->active].rows;
    for (int r = 0; r < rows; r++) {
        char *t = vt_line_text(vt, vt_line_index_for_row(vt, r));
        sb_append(&sb, t);
        free(t);
        if (r != rows - 1) sb_append(&sb, "\n");
    }
    return sb_detach(&sb);
}

char *vt_selection_text(VT *vt, int l0, int c0, int l1, int c1) {
    if (l0 > l1) { int t = l0; l0 = l1; l1 = t; t = c0; c0 = c1; c1 = t; }
    else if (l0 == l1 && c0 > c1) { int t = c0; c0 = c1; c1 = t; }
    StrBuf sb; sb_init(&sb);
    int cols = vt->scr[vt->active].cols;
    for (int line = l0; line <= l1; line++) {
        Cell *cells = vt_line(vt, line);
        if (!cells) continue;
        int x0 = (line == l0) ? c0 : 0;
        int x1 = (line == l1) ? c1 : cols - 1;
        if (x0 < 0) x0 = 0;
        if (x1 >= cols) x1 = cols - 1;
        /* 去掉行尾空白 */
        int end = x1;
        while (end >= x0 && (cells[end].cp == ' ' || cells[end].cp == 0) && !cells[end].cp2) end--;
        for (int x = x0; x <= end; x++) {
            Cell *c = &cells[x];
            if (c->width == 0) continue;
            char tmp[8];
            size_t n = utf8_encode_one(c->cp ? c->cp : ' ', tmp);
            sb_append_n(&sb, tmp, n);
            if (c->cp2) { size_t n2 = utf8_encode_one(c->cp2, tmp); sb_append_n(&sb, tmp, n2); }
        }
        if (line != l1) sb_append(&sb, "\r\n");
    }
    return sb_detach(&sb);
}

int vt_find(VT *vt, const char *needle, int start_line, int *out_line, int *out_col) {
    if (!needle || !*needle) return 0;
    int total = vt_total_lines(vt);
    for (int pass = 0; pass < 2; pass++) {
        int from = pass == 0 ? start_line : 0;
        int to = pass == 0 ? total : start_line;
        for (int i = from; i < to; i++) {
            char *t = vt_line_text(vt, i);
            char *p = t;
            int found = 0;
            while (*p) {
                if (_strnicmp(p, needle, strlen(needle)) == 0) { found = 1; break; }
                p++;
            }
            if (found) {
                /* 字节偏移 -> 单元格列 */
                int byte_off = (int)(p - t);
                int x = 0, off = 0;
                Cell *cells = vt_line(vt, i);
                int cols = vt->scr[vt->active].cols;
                while (x < cols && off < byte_off) {
                    if (cells[x].width != 0) {
                        char tmp[8];
                        off += (int)utf8_encode_one(cells[x].cp ? cells[x].cp : ' ', tmp);
                        if (cells[x].cp2) off += (int)utf8_encode_one(cells[x].cp2, tmp);
                    }
                    x++;
                }
                if (out_line) *out_line = i;
                if (out_col) *out_col = x;
                free(t);
                return 1;
            }
            free(t);
        }
    }
    return 0;
}

const char *vt_title(VT *vt) { return vt->title.buf; }
int vt_title_dirty(VT *vt) { return vt->title_dirty; }
void vt_clear_title_dirty(VT *vt) { vt->title_dirty = 0; }
void vt_set_title(VT *vt, const char *title) { set_title_internal(vt, title); }

void vt_set_user(VT *vt, void *user) { vt->user = user; }
void vt_set_write_cb(VT *vt, VTWriteFn fn) { vt->write_cb = fn; }
void vt_set_bell_cb(VT *vt, VTSimpleFn fn) { vt->bell_cb = fn; }
void vt_set_title_cb(VT *vt, VTTitleFn fn) { vt->title_cb = fn; }
void vt_set_notify_cb(VT *vt, VTNotifyFn fn) { vt->notify_cb = fn; }
void vt_set_clip_cb(VT *vt, VTClipFn fn) { vt->clip_cb = fn; }
