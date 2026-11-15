/* YTerminal - app.h : 全局状态与跨模块接口 */
#ifndef YT_APP_H
#define YT_APP_H

#include "session.h"
#include "theme.h"
#include "vt.h"
#include "backend.h"

/* 视图 -> 主窗口消息 */
#define WM_YT_TITLE_CHANGED (WM_APP + 30)   /* wParam = view hwnd，标题由 term_view_title 获取 */
#define WM_YT_VIEW_STATUS   (WM_APP + 31)   /* wParam = view hwnd */
#define WM_YT_SIDEBAR_REFRESH (WM_APP + 32)
#define WM_YT_APPLY_SETTINGS  (WM_APP + 33)

#define YT_TOOLBAR_H 42
#define YT_TAB_H 34
#define YT_STATUS_H 26

typedef struct App {
    HINSTANCE inst;
    HWND main;
    HWND toolbar;
    HWND tabbar;
    HWND sidebar;
    HWND statusbar;
    Settings set;
    SessionList sessions;
    Theme *theme;
    UINT dpi;
    HFONT f_latin, f_latin_b, f_latin_i, f_latin_bi;
    HFONT f_cjk, f_cjk_b, f_cjk_i;
    HFONT f_ui, f_ui_b;
    int cell_w, cell_h, font_ascent, font_h, font_pad;
    int broadcast;
    int closing;
    int tab_count;
    HWND roots[128];      /* 每个标签的根窗口（视图或分屏） */
    int active_tab;
    int show_sidebar;
    char pending_sessions[64][48]; /* 恢复用 */
    int pending_count;
} App;

extern App g_app;

void  app_init(HINSTANCE inst);
void  app_shutdown(void);
int   app_dpi(int px);
void  app_rebuild_fonts(void);
void  app_apply_theme(void);

Theme *app_theme(void);
COLORREF app_cr(u32 color, u32 default_color);
COLORREF app_ui_bg(void);
COLORREF app_ui_panel(void);
COLORREF app_ui_border(void);
COLORREF app_ui_text(void);
COLORREF app_ui_dim(void);
COLORREF app_ui_accent(void);
COLORREF app_ui_hover(void);

/* 视图/标签 */
HWND  app_open_session(const Session *s, int activate);
void  app_close_tab(int index);
void  app_activate_tab(int index);
int   app_find_tab(HWND root);
HWND  app_active_root(void);
HWND  app_active_view(void);
void  app_replace_root(HWND old_root, HWND new_root);
void  app_add_root(HWND root);
void  app_remove_root(HWND root);
void  app_layout(void);
void  app_update_tab_title(HWND view);
void  app_refresh_tabbar(void);

/* 输入广播 */
void  app_dispatch_input(HWND from, const void *data, size_t len);

/* 配色/绘图 */
void  app_fill(HDC hdc, RECT *rc, COLORREF color);
void  app_fill_round(HDC hdc, RECT *rc, int radius, COLORREF color);
void  app_draw_text(HDC hdc, const wchar_t *text, RECT *rc, HFONT font, COLORREF color, UINT flags);
int   app_text_width(HDC hdc, HFONT font, const wchar_t *text);
void  app_draw_icon(HDC hdc, const wchar_t *glyph, RECT *rc, COLORREF color, int size_pt);

/* 剪贴板 */
int   app_clipboard_set(const char *utf8);
char *app_clipboard_get(void);          /* UTF-8，调用者 free */
int   app_clipboard_has_text(void);

/* 状态栏 */
void  app_status_set(const wchar_t *text);
void  app_status_refresh(void);

/* 会话 */
void  app_connect_session(int session_index);
void  app_show_session_dialog(HWND parent, int session_index);
void  app_show_settings(HWND parent);
void  app_show_quick_connect(HWND parent);
void  app_show_about(HWND parent);
void  app_import_sessions(HWND parent);
void  app_quick_commands(HWND parent);
void  app_show_sftp(HWND parent);
void  app_split_active(int vertical);
void  app_toggle_broadcast(void);
void  app_find_in_active(void);
void  app_save_workspace(void);
void  app_restore_workspace(void);
void  app_sessions_changed(void);
void  app_command(UINT id);
void  app_rename_active_tab(HWND parent);
void  app_show_palette(HWND parent);
void  app_close_other_tabs(void);
void  app_close_tabs_right(void);
void  app_show_shortcuts(HWND parent);
void  app_show_tunnels(HWND parent);
HWND  main_window_create(HINSTANCE inst);

/* 各 UI 模块 */
HWND  toolbar_create(HWND parent, HINSTANCE inst);
void  toolbar_resize(HWND h, RECT *rc);
HWND  sidebar_create(HWND parent, HINSTANCE inst);
void  sidebar_refresh(HWND h);
void  sidebar_resize(HWND h, RECT *rc);
HWND  statusbar_create(HWND parent, HINSTANCE inst);
void  statusbar_resize(HWND h, RECT *rc);
void  statusbar_update(HWND h);
HWND  split_create(HWND parent, HWND a, HWND b, int vertical);
void  split_destroy(HWND split);
void  split_set_ratio(HWND split, float ratio);
int   split_is_split(HWND h);

/* 标签栏 */
HWND  tabbar_create(HWND parent, HINSTANCE inst);
void  tabbar_resize(HWND h, RECT *rc);
void  tabbar_refresh(HWND h);

/* 根窗口管理 */
void  app_destroy_root(HWND root);
HWND  app_root_view(HWND root);
void  app_root_title(int index, wchar_t *buf, int cch);
void  app_layout_content(void);

#endif
