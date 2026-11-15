/* YTerminal - backend.h : 会话后端（本地 ConPTY / SSH / 串口 / Telnet） */
#ifndef YT_BACKEND_H
#define YT_BACKEND_H

#include "session.h"

/* 通知主窗口的消息 */
#define WM_YT_BACK_DATA   (WM_APP + 10)   /* wParam = backend id, lParam = BackData* */
#define WM_YT_BACK_STATE  (WM_APP + 11)   /* wParam = backend id, lParam = 状态码 */
#define WM_YT_BACK_ERROR  (WM_APP + 12)   /* wParam = backend id, lParam = char*（堆） */

enum { BK_LOCAL = 0, BK_SSH = 1, BK_SERIAL = 2, BK_TELNET = 3 };
enum { BS_IDLE = 0, BS_CONNECTING = 1, BS_CONNECTED = 2, BS_CLOSED = 3, BS_ERROR = 4 };

typedef struct BackData {
    int id;
    size_t len;
    u8 data[1];
} BackData;

typedef struct Backend Backend;

void      backend_init(void);
void      backend_shutdown(void);
Backend  *backend_create(const Session *s, HWND notify, int cols, int rows);
BOOL      backend_start(Backend *b);
void      backend_close(Backend *b);
BOOL      backend_write(Backend *b, const void *data, size_t len);
void      backend_resize(Backend *b, int cols, int rows);
void      backend_send_break(Backend *b);
void      backend_set_line(Backend *b, int dtr, int rts);
int       backend_state(Backend *b);
int       backend_id(Backend *b);
int       backend_kind(Backend *b);
const char *backend_status(Backend *b);
const char *backend_error(Backend *b);
Session  *backend_session(Backend *b);
int       backend_can_sftp(Backend *b);
int       backend_has_modem_lines(Backend *b);
int       backend_has_break(Backend *b);
Backend  *backend_from_id(int id);

/* 供测试使用：构造 ssh 命令行（调用者 free） */
char     *backend_build_ssh_command(const Session *s, int cols, int rows);

#endif
