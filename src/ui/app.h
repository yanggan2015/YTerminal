#ifndef YT_APP_H
#define YT_APP_H
/* 主窗共享状态与跨编译单元接口；新功能放独立 .c，勿再堆进 main.c */
#include "yt_core.h"

#define YT_BB_N 6

typedef struct {
    HWND hwnd, tab, tree, sftp, status;
    HWND sm_cap, sftp_cap;
    HWND cmb, host, port, user, pass, com, baud, jump, fwd, key, shell;
    HWND find, sftp_path, quick, btn_quick;
    HWND btn_sref, btn_sup, btn_sdn;
    HWND tb[9], cb_go, bb[YT_BB_N];
    HFONT font_ui, font_ui_bold;
    HBRUSH brush_chrome, brush_cap, brush_white, brush_status;
    HINSTANCE hi;
    int ntabs, tree_n;
    HWND terms[YT_MAX_TABS];
    YtSession *sess[YT_MAX_TABS];
    YtSession profiles[YT_MAX_SESSIONS];
    int split_mode;
    BOOL sftp_visible, sm_visible, bb_visible;
    BOOL tb_visible, cb_visible, st_visible;
    BOOL always_on_top, fullscreen;
    WINDOWPLACEMENT fs_wp;
    int opacity;
    int theme;
    int font_px;
    char sftp_cwd[512];
    int tab_color_cycle;
    BOOL stats_on;
    wchar_t last_find[128];
} App;

extern App g;

void yt_app_set_status(const wchar_t *s);
int  yt_app_cur_tab(void);
int  yt_app_kind_sel(void);
void yt_app_layout(void);
int  yt_app_add_tab(YtSession *s);
void yt_app_close_tab(int idx);
void yt_app_start_ok(YtSession *s, BOOL ok);

/* app_tree.c */
void yt_app_tree_reload(void);
void yt_app_sessions_reload(void);
void yt_app_sessions_persist(void);
void yt_app_profile_from_session(YtSession *dst, const YtSession *src);
int  yt_app_tree_sel_index(void);
void yt_app_tree_connect_index(int i);
void yt_app_tree_delete_index(int i);

/* app_connect.c */
void yt_app_fill_from_ui(YtSession *s);
void yt_app_do_connect(void);
void yt_app_start_local(const wchar_t *shell, BOOL admin);
void yt_app_clone_cur(void);
void yt_app_reconnect_cur(void);

#endif
