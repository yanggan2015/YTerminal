/* YTerminal - session.h : 会话模型 / 设置 / 持久化 */
#ifndef YT_SESSION_H
#define YT_SESSION_H

#include "util.h"

/* 协议 */
enum { PROTO_SSH = 0, PROTO_SERIAL = 1, PROTO_TELNET = 2, PROTO_LOCAL = 3 };

/* SSH 认证方式 */
enum { AUTH_PASSWORD = 0, AUTH_KEY = 1, AUTH_AGENT = 2, AUTH_KEYBOARD = 3 };

typedef struct Session {
    char id[48];
    char name[128];
    char group[160];
    int  protocol;
    int  favorite;
    char note[256];

    /* SSH / Telnet */
    char host[256];
    int  port;
    char user[96];
    int  auth;
    char keyfile[MAX_PATH * 2];
    char password[256];        /* 运行时明文；落盘时 DPAPI 加密 */
    char password_enc[1024];
    int  keepalive;
    int  compression;
    char jump[256];
    char extra_args[512];
    char tunnels[512];

    /* 串口 */
    char port_name[32];
    int  baud;
    int  databits;
    int  parity;      /* 0=N 1=O 2=E 3=M 4=S */
    int  stopbits;    /* 1, 2 */
    int  flow;        /* 1=xon/xoff, 2=rts/cts, 4=dsr/dtr */
    int  dtr;
    int  rts;
    int  local_echo;

    /* 本地 Shell */
    char shell[512];
    wchar_t cwd[MAX_PATH * 2];

    /* 快捷命令与触发器（每行一条） */
    char quick_cmds[1024];
    char triggers[512];

    /* 终端外观（0 = 跟随全局设置） */
    int  cols, rows;
    int  encoding;     /* 0=UTF-8 1=GBK 2=BIG5 3=Shift-JIS 4=EUC-KR */
} Session;

typedef struct {
    Session *items;
    int n, cap;
} SessionList;

typedef struct {
    int theme;
    char font[64];
    int font_size;
    int line_pad;
    int cursor_style;      /* 0 块闪烁 1 块 2 下划线 3 竖线 */
    int cursor_blink;
    int scrollback;
    int copy_on_select;
    int right_click_paste;
    int confirm_paste;
    int restore_tabs;
    int save_on_exit;
    int bell;
    int opacity;           /* 30..100 */
    int show_sidebar;
    int show_statusbar;
    int show_toolbar;
    int sidebar_width;
    int tab_width;
    int wheel_zoom;
    int alt_send_esc;
    int broadcast_default;
    int log_auto;
    wchar_t log_dir[MAX_PATH * 2];
    wchar_t download_dir[MAX_PATH * 2];
    int ui_lang;
    int ssh_keepalive;
    int double_click_copy;
    int middle_click_paste;
    int show_tab_close;
    int confirm_close;
    int smooth_scroll;
    int status_clock;
    char highlights[512];  /* 高亮规则：文本=>#RRGGBB，分号/换行分隔 */
} Settings;

/* ---- 数据目录 ---- */
void yt_data_dir(wchar_t *out, int cch);
int  yt_is_portable(void);

/* ---- 设置 ---- */
void     settings_defaults(Settings *s);
BOOL     settings_load(Settings *s);
BOOL     settings_save(Settings *s);

/* ---- 会话 ---- */
void     sessions_init(SessionList *list);
void     sessions_free(SessionList *list);
Session *sessions_add(SessionList *list, const Session *src);
void     sessions_remove(SessionList *list, int index);
Session *sessions_find(SessionList *list, const char *id);
int      sessions_find_index(SessionList *list, const char *id);
Session  session_defaults(int protocol);
BOOL     sessions_load(SessionList *list);
BOOL     sessions_save(SessionList *list);
const char *protocol_name(int proto);
const char *protocol_key(int proto);
int      protocol_from_key(const char *key);
void     session_generate_id(Session *s);
char    *session_subtitle(Session *s, char *buf, int cch);

/* 凭据：DPAPI 加密（返回十六进制字符串，调用者 free） */
char *yt_dpapi_encrypt_hex(const char *plain);
char *yt_dpapi_decrypt_hex(const char *hex);

#endif

