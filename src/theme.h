/* YTerminal - theme.h : 终端与界面配色主题 */
#ifndef YT_THEME_H
#define YT_THEME_H

#include "vt.h"

typedef struct {
    const char *name;
    const char *name_en;
    u32 fg, bg, cursor, cursor_text, selection, selection_text;
    u32 ansi[16];
    u32 ui_bg, ui_panel, ui_border, ui_text, ui_dim, ui_accent, ui_hover;
} Theme;

int theme_count(void);
Theme *theme_get(int idx);
u32 theme_color_256(Theme *t, int index);   /* 256 色 xterm 调色板 */
int theme_default(void);

#endif

