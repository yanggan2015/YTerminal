/* YTerminal - vt.h : VT100/xterm 终端模拟器核心（纯 C，无依赖） */
#ifndef YT_VT_H
#define YT_VT_H

#include "util.h"

/* ---- 颜色编码 ----
 * 0x00000000..0x000000FF : 256 色调色板索引
 * 0x01000000 | RGB       : 24 位真彩色
 * 0x02000000 / ...01     : 默认前景/背景
 */
#define VT_COL_RGB_FLAG   0x01000000u
#define VT_COL_DEFAULT    0x02000000u
#define VT_DEFAULT_FG     (VT_COL_DEFAULT | 0u)
#define VT_DEFAULT_BG     (VT_COL_DEFAULT | 1u)
#define VT_RGB(r,g,b)     (VT_COL_RGB_FLAG | (((u32)(r)) << 16) | (((u32)(g)) << 8) | ((u32)(b)))
#define VT_IS_RGB(c)      (((c) & VT_COL_RGB_FLAG) != 0)
#define VT_IS_DEFAULT(c)  (((c) & VT_COL_DEFAULT) == VT_COL_DEFAULT)
#define VT_INDEX(c)       ((int)((c) & 0xFF))

/* ---- 属性位 ---- */
#define ATTR_BOLD      0x01
#define ATTR_DIM       0x02
#define ATTR_ITALIC    0x04
#define ATTR_UNDERLINE 0x08
#define ATTR_BLINK     0x10
#define ATTR_INVERSE   0x20
#define ATTR_STRIKE    0x40
#define ATTR_HIDDEN    0x80

typedef struct Cell {
    u32 cp;      /* Unicode 码点 */
    u32 cp2;     /* 组合符号（可选） */
    u32 fg;
    u32 bg;
    u8  attr;
    u8  width;   /* 1=单宽 2=双宽 0=双宽占位 */
    u16 pad;
} Cell;

typedef struct VT VT;

/* 回调：由宿主注入 */
typedef void (*VTWriteFn)(void *user, const char *s, size_t n);          /* 回写应答序列 */
typedef void (*VTSimpleFn)(void *user);                                  /* 响铃 */
typedef void (*VTTitleFn)(void *user, const char *title);                /* 标题 */
typedef void (*VTNotifyFn)(void *user, const char *title, const char *body); /* 通知 */
typedef void (*VTClipFn)(void *user, const char *text);                  /* 剪贴板写入 */

VT  *vt_create(int cols, int rows, int scrollback);
void vt_destroy(VT *vt);
void vt_resize(VT *vt, int cols, int rows);
void vt_feed(VT *vt, const u8 *data, size_t len);
void vt_reset(VT *vt);

/* 显示信息 */
int   vt_cols(VT *vt);
int   vt_rows(VT *vt);
int   vt_history_len(VT *vt);
int   vt_total_lines(VT *vt);                 /* history + rows */
int   vt_view_offset(VT *vt);                 /* 0 = 底部（实时） */
void  vt_scroll(VT *vt, int delta_lines);     /* >0 向历史方向滚动 */
void  vt_scroll_to_bottom(VT *vt);
Cell *vt_line(VT *vt, int index);             /* 0..total-1，返回 cols 个 Cell */
int   vt_line_index_for_row(VT *vt, int row); /* 视口行 -> 全局行 */
int   vt_cursor_x(VT *vt);
int   vt_cursor_y(VT *vt);
int   vt_cursor_visible(VT *vt);
int   vt_cursor_style(VT *vt);                /* 0块 1下划线 2竖线 */
int   vt_is_alt(VT *vt);
int   vt_bracket_paste(VT *vt);
int   vt_app_cursor(VT *vt);
int   vt_mouse_mode(VT *vt);                  /* 0关 1普通 2按键 3任意 */
int   vt_mouse_sgr(VT *vt);
int   vt_focus_events(VT *vt);
u32   vt_cursor_fg(VT *vt);
u32   vt_cursor_bg(VT *vt);

/* 文本 */
char *vt_line_text(VT *vt, int index);                       /* 调用者 free */
char *vt_selection_text(VT *vt, int l0, int c0, int l1, int c1);
char *vt_screen_text(VT *vt);                                /* 整个视口，测试用 */
int   vt_find(VT *vt, const char *needle, int start_line, int *out_line, int *out_col);
void  vt_clear_history(VT *vt);
void  vt_set_title(VT *vt, const char *title);
const char *vt_title(VT *vt);
int   vt_title_dirty(VT *vt);
void  vt_clear_title_dirty(VT *vt);

/* 宿主回调设置 */
void vt_set_user(VT *vt, void *user);
void vt_set_write_cb(VT *vt, VTWriteFn fn);
void vt_set_bell_cb(VT *vt, VTSimpleFn fn);
void vt_set_title_cb(VT *vt, VTTitleFn fn);
void vt_set_notify_cb(VT *vt, VTNotifyFn fn);
void vt_set_clip_cb(VT *vt, VTClipFn fn);

#endif
