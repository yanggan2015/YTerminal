#ifndef YT_CORE_H
#define YT_CORE_H

#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define YT_VERSION       L"1.0.0"
#define YT_MAX_TABS      64
#define YT_MAX_SESSIONS  512
#define YT_COLS          120
#define YT_ROWS          40
#define YT_SCROLLBACK    10000
#define YT_PATH_MAX      520
#define YT_TITLE_MAX     96
#define YT_HOST_MAX      256

#define WM_YT_OUTPUT     (WM_APP + 200)
#define WM_YT_CLOSED     (WM_APP + 201)
#define WM_YT_TITLE      (WM_APP + 202)
#define WM_YT_SFTP_DONE  (WM_APP + 203)
#define WM_YT_FWD_EVENT  (WM_APP + 204)
#define WM_YT_SSH_DONE   (WM_APP + 210)
#define WM_YT_SFTP_REFRESH (WM_APP + 211)
#define WM_YT_FOCUS_TERM (WM_APP + 212)

typedef enum {
    YT_KIND_LOCAL = 0,
    YT_KIND_SSH,
    YT_KIND_SERIAL,
    YT_KIND_TELNET,
    YT_KIND_X11
} YtKind;

typedef enum {
    YT_ST_IDLE = 0,
    YT_ST_CONNECTING,
    YT_ST_CONNECTED,
    YT_ST_CLOSED,
    YT_ST_ERROR
} YtState;

typedef struct YtSession YtSession;

typedef void (*YtWriteFn)(YtSession *s, const char *data, int len);
typedef void (*YtResizeFn)(YtSession *s, int cols, int rows);
typedef void (*YtCloseFn)(YtSession *s);

struct YtSession {
    YtKind kind;
    YtState state;
    wchar_t title[YT_TITLE_MAX];
    wchar_t name[YT_TITLE_MAX];
    wchar_t group[64];
    HWND hwnd_term;
    HWND hwnd_main;
    int tab_index;
    int split_with; /* peer tab for sync input; -1 none */
    CRITICAL_SECTION lock;
    YtWriteFn write_fn;
    YtResizeFn resize_fn;
    YtCloseFn close_fn;
    void *impl;
    char err[256];
    wchar_t host[YT_HOST_MAX];
    int port;
    wchar_t user[64];
    wchar_t pass[128];
    wchar_t keyfile[YT_PATH_MAX];
    wchar_t jump_host[YT_HOST_MAX];
    int jump_port;
    wchar_t jump_user[64];
    wchar_t jump_pass[128];
    wchar_t shell[64];
    wchar_t com[32];
    int baud;
    int data_bits;
    int stop_bits;
    int parity; /* 0 none 1 odd 2 even */
    int flow;   /* 0 none 1 rtscts 2 xonxoff */
    /* forwards: local_port:host:port comma-separated ASCII */
    char forwards[512];
    BOOL log_on;
    BOOL sync_input;
    BOOL locked; /* 锁定输入防误操作 */
    FILE *log_fp;
    void *sftp; /* SFTP handle when SSH */
    void *ring; /* YtRing* 输出环形缓冲，供 Agent 增量读 */
};

typedef struct {
    wchar_t root[YT_PATH_MAX];
    wchar_t config[YT_PATH_MAX];
    wchar_t sessions[YT_PATH_MAX]; /* 连接/会话书签 */
    wchar_t window[YT_PATH_MAX];   /* 窗口/工作台 */
    wchar_t creds[YT_PATH_MAX];
    wchar_t logs[YT_PATH_MAX];
    wchar_t themes[YT_PATH_MAX];
    wchar_t runtime[YT_PATH_MAX];
    wchar_t keys[YT_PATH_MAX];
} YtPaths;

typedef struct {
    int x, y, w, h;
    BOOL maximized;
    BOOL sm_visible, bb_visible, sftp_visible;
    int split;
    int theme;
    int font_px;
    int opacity; /* 180-255 */
    int kind;
    BOOL stats;
    int paste_delay_ms;
} YtWindowCfg;

void yt_paths_init(YtPaths *p);
const YtPaths *yt_paths(void);

void yt_session_init(YtSession *s);
void yt_session_free(YtSession *s);
void yt_session_write(YtSession *s, const char *data, int len);
void yt_session_resize(YtSession *s, int cols, int rows);
void yt_session_close(YtSession *s);
void yt_session_post_out(YtSession *s, const char *buf, int n);

BOOL yt_cred_save(const wchar_t *id, const wchar_t *secret);
BOOL yt_cred_load(const wchar_t *id, wchar_t *out, int cap);
BOOL yt_cred_export_master(const wchar_t *master, const wchar_t *out_path);
BOOL yt_cred_import_master(const wchar_t *master, const wchar_t *in_path);

int  yt_profile_save(const YtSession *list, int n);
int  yt_profile_load(YtSession *out, int cap);
/* 配置为空时写入测试机等默认书签，返回条数 */
int  yt_profile_ensure_defaults(YtSession *out, int cap);

BOOL yt_ssh_start(YtSession *s);
BOOL yt_ssh_start_async(HWND hwnd, YtSession *s);
BOOL yt_local_start(YtSession *s);
BOOL yt_serial_start(YtSession *s);
BOOL yt_telnet_start(YtSession *s);
BOOL yt_x11_ensure(void);

void *yt_sftp_open(YtSession *s);
void  yt_sftp_close(void *sftp);
int   yt_sftp_listdir(void *sftp, const char *path, char *out, int cap);
int   yt_sftp_download(void *sftp, const char *remote, const wchar_t *local);
int   yt_sftp_upload(void *sftp, const wchar_t *local, const char *remote);

BOOL yt_fwd_start(YtSession *s);
void yt_fwd_stop(YtSession *s);

int  yt_zmodem_rx(YtSession *s, const wchar_t *dir);
int  yt_zmodem_tx(YtSession *s, const wchar_t *file);

void yt_sync_broadcast(YtSession **tabs, int n, int from, const char *data, int len);
/* UI sets this so terminal can fan-out sync input */
void yt_app_set_sync_hook(void (*hook)(YtSession *from, const char *data, int len));
void yt_app_sync_write(YtSession *from, const char *data, int len);

HWND term_create(HWND parent, int id, HINSTANCE hi);
void term_attach(HWND hw, YtSession *s);
void term_append(HWND hw, const char *utf8, int len);
void term_clear(HWND hw);
void term_find(HWND hw, const wchar_t *text);
void term_find_next(HWND hw);
void term_set_focus(HWND hw);
void term_get_cursor(HWND hw, int *row, int *col, int *lines);
BOOL term_dump(HWND hw, const wchar_t *path);
void term_set_font_px(HWND hw, int px);
int  term_get_font_px(HWND hw);
void term_copy(HWND hw);
void term_paste(HWND hw);
void term_select_all(HWND hw);
YtSession *term_session(HWND hw);

int yt_dlg_session_prop(HWND parent, HINSTANCE hi, YtSession *s);

BOOL yt_window_save(const YtWindowCfg *c);
BOOL yt_window_load(YtWindowCfg *c);
BOOL yt_layout_save(int ntabs, int cur, int split, const int *kinds, int nk);
BOOL yt_layout_load(int *ntabs, int *cur, int *split, int *kinds, int cap, int *nk);

int  yt_com_enum(wchar_t names[][32], int cap);
void yt_macro_rec_start(void);
void yt_macro_rec_stop(void);
BOOL yt_macro_recording(void);
void yt_macro_push(const wchar_t *line);
int  yt_macro_count(void);
const wchar_t *yt_macro_get(int i);
BOOL yt_macro_save(void);
int  yt_macro_load(void);
void yt_hist_add(const wchar_t *cmd);
int  yt_hist_complete(const wchar_t *prefix, wchar_t *out, int cap);
BOOL yt_script_run_file(YtSession *s, const wchar_t *path);
void yt_sys_stats(wchar_t *out, int cap);
void yt_set_paste_delay(int ms);
int  yt_get_paste_delay(void);

#endif
