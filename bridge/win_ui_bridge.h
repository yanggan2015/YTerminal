/* win_ui_bridge.h — 给纯 C / Win32 桌面程序加一层「AI 可读 + 可操作」的桥接层。
 *
 * 只在 Debug / Test 构建里定义 UI_BRIDGE_ENABLE 并编译 win_ui_bridge.c；
 * Release 构建不定义该宏，所有调用自动退化为空操作，版本里没有 HTTP 端口。
 * HTTP 只监听 127.0.0.1，不对外暴露。
 *
 * 接入三步：
 *   1. 主窗口创建后调用 ui_bridge_start(hwnd, 8086)
 *   2. 每个控件创建后调用 ui_bridge_set_id(ctl, "btn.calc")
 *   3. 在 WndProc 开头加：if (ui_bridge_handle_message(...)) return 0;
 */
#ifndef WIN_UI_BRIDGE_H
#define WIN_UI_BRIDGE_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 桥接层把 HTTP 线程收到的动作转投到 UI 线程执行时使用的自定义消息。 */
#define UI_BRIDGE_WM_ACTION (WM_APP + 0x0501)

#ifndef UI_BRIDGE_ENABLE
/* ---- Release 兜底：不编译桥接层，业务代码不需要 #ifdef ---- */
#define ui_bridge_start(...)              (0)
#define ui_bridge_stop()                  ((void) 0)
#define ui_bridge_set_token(...)          ((void) 0)
#define ui_bridge_set_state_provider(...) ((void) 0)
#define ui_bridge_set_id(h, id)           ((BOOL) 0)
#define ui_bridge_add_virtual(...)        ((BOOL) 0)
#define ui_bridge_log(...)                ((void) 0)
#define ui_bridge_handle_message(...)     ((BOOL) 0)

#else
/* ---- Test 构建：真实接口 ---- */

/* 业务状态提供者：把内部变量序列化为 JSON 对象字符串写入 out。 */
typedef void (*UI_STATE_CB)(char *out, int cap, void *user);

/* 自绘控件的点击回调（在 UI 线程执行）。 */
typedef void (*UI_VIRTUAL_CLICK)(void *user);

/* 启动/停止桥接层。port 选固定值，避免与本机其他服务冲突。 */
int  ui_bridge_start(HWND hwnd_main, int port);
void ui_bridge_stop(void);

/* 可选：设置访问令牌，设置后所有请求需带 X-Ui-Token 头。 */
void ui_bridge_set_token(const char *token);

/* 可选：注册业务状态回调，GET /state 会合并其输出。 */
void ui_bridge_set_state_provider(UI_STATE_CB cb, void *user);

/* 给控件登记稳定 ID。id 字符串需长期有效（建议字面量），不要传栈上缓冲区。 */
BOOL ui_bridge_set_id(HWND ctl, const char *id);

/* 把自绘控件登记为虚拟控件，让 AI 在 /ui-tree 里看到并可以 click。
 * rc 用屏幕坐标（与其它控件一致）。 */
BOOL ui_bridge_add_virtual(const char *id, const char *role, const char *name,
                           RECT rc, BOOL enabled, const char *text,
                           UI_VIRTUAL_CLICK on_click, void *user);

/* 任意线程可调用，写入 /logs 环形缓冲。 */
void ui_bridge_log(const char *fmt, ...);

/* 在 WndProc 开头调用。返回 TRUE 表示消息已被消费。 */
BOOL ui_bridge_handle_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

/* 读取当前桥接令牌（只读缓冲，可能为空）。 */
const char *ui_bridge_token(void);

#endif /* UI_BRIDGE_ENABLE */

#ifdef __cplusplus
}
#endif
#endif /* WIN_UI_BRIDGE_H */
