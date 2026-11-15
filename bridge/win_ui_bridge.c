/* win_ui_bridge.c — Win32 纯 C 程序的 AI 可测桥接层实现。
 * 依赖：mongoose（单文件 C HTTP 库，v7.x）、cJSON（单文件 C JSON 库）。
 * 只在 Debug / Test 构建编译本文件。
 */
#include "win_ui_bridge.h"

#if !defined(UI_BRIDGE_ENABLE)
#error "win_ui_bridge.c 只能在定义了 UI_BRIDGE_ENABLE 的 Test 构建中编译"
#endif

#include "cJSON.h"
#include "mongoose.h"
#include "yt_agent.h"

#include <commctrl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UB_ID_LEN      96
#define UB_LOG_MAX     128
#define UB_LOG_LEN     512
#define UB_VIRTUAL_MAX 64
#define UB_WAIT_MS     8000

#pragma comment(lib, "comctl32.lib")

/* ------------------------------------------------------------------ 日志 */

typedef struct {
    unsigned seq;
    unsigned long ms;
    char text[UB_LOG_LEN];
} UB_LOG_ENTRY;

static UB_LOG_ENTRY g_logs[UB_LOG_MAX];
static unsigned     g_log_seq;

/* --------------------------------------------------------------- 全局状态 */

typedef struct {
    char id[UB_ID_LEN];
    char role[32];
    char name[256];
    char text[512];
    RECT rect;
    BOOL enabled;
    UI_VIRTUAL_CLICK on_click;
    void *user;
} UB_VIRTUAL;

typedef struct {
    char id[UB_ID_LEN];
    char action[32];
    char text[512];
    int  value;
} UB_REQUEST;

static HWND              g_main;
static volatile LONG     g_running;
static volatile LONG     g_inited;
static volatile LONG     g_action_count;
static CRITICAL_SECTION  g_lock;
static HANDLE            g_done_event;

static UB_REQUEST        g_req;
static volatile LONG     g_req_pending;
static int               g_res_code;
static char              g_res_msg[512];
static char              g_last_action[64];

static UB_VIRTUAL        g_virtual[UB_VIRTUAL_MAX];
static int               g_virtual_count;

static UI_STATE_CB       g_state_cb;
static void              *g_state_user;
static char              g_token[64];
static HANDLE            g_http_thr;

/* ------------------------------------------------------------------ 工具 */

static void ub_copy(char *dst, int cap, const char *src)
{
    if (cap <= 0) return;
    if (!src) { dst[0] = '\0'; return; }
    int n = (int) strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, (size_t) n);
    dst[n] = '\0';
}

static void ub_wide_to_utf8(const WCHAR *w, char *out, int cap)
{
    out[0] = '\0';
    if (!w || !w[0] || cap <= 1) return;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, NULL, NULL);
}

static void ub_utf8_to_wide(const char *s, WCHAR *out, int cap_w)
{
    out[0] = L'\0';
    if (!s) return;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap_w);
}

static void ub_class_name(HWND h, char *out, int cap)
{
    WCHAR w[128];
    out[0] = '\0';
    if (!h) return;
    GetClassNameW(h, w, (int) (sizeof(w) / sizeof(w[0])));
    ub_wide_to_utf8(w, out, cap);
}

static void ub_window_text(HWND h, char *out, int cap)
{
    WCHAR w[512];
    out[0] = '\0';
    if (!h) return;
    GetWindowTextW(h, w, (int) (sizeof(w) / sizeof(w[0])));
    ub_wide_to_utf8(w, out, cap);
}

static const char *ub_role_of(const char *cls)
{
    if (!cls || !cls[0]) return "unknown";
    if (strstr(cls, "Button"))      return "button";
    if (strstr(cls, "Edit"))        return "textbox";
    if (strstr(cls, "RICHEDIT"))    return "textbox";
    if (strstr(cls, "ComboBox"))    return "combobox";
    if (strstr(cls, "ListBox"))     return "listbox";
    if (strstr(cls, "SysListView")) return "list";
    if (strstr(cls, "SysTreeView")) return "tree";
    if (strstr(cls, "Static"))      return "label";
    if (strstr(cls, "TrackBar"))    return "slider";
    if (strstr(cls, "UpDown"))      return "spinner";
    if (strstr(cls, "Progress"))    return "progress";
    if (strstr(cls, "StatusBar"))   return "statusbar";
    if (strstr(cls, "TabControl"))  return "tabview";
    return "unknown";
}

/* 稳定 ID：优先取 ui_bridge_set_id 登记的属性，否则用 类名#控件ID 兜底。
 * 两处（导树 / 定位）必须用同一个函数，保证 AI 看到的 id 可回指。 */
static void ub_effective_id(HWND h, char *out, int cap)
{
    const char *pid = (const char *) GetPropA(h, "ui.id");
    if (pid && pid[0]) {
        ub_copy(out, cap, pid);
        return;
    }
    char cls[128];
    ub_class_name(h, cls, sizeof(cls));
    snprintf(out, (size_t) cap, "auto/%s#%ld", cls[0] ? cls : "window",
             (long) GetDlgCtrlID(h));
}

/* ------------------------------------------------------------------ 日志 */

void ui_bridge_log(const char *fmt, ...)
{
    char msg[UB_LOG_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (!g_inited) return;

    EnterCriticalSection(&g_lock);
    UB_LOG_ENTRY *e = &g_logs[g_log_seq % UB_LOG_MAX];
    e->seq = g_log_seq;
    e->ms  = (unsigned long) GetTickCount();
    ub_copy(e->text, UB_LOG_LEN, msg);
    g_log_seq++;
    LeaveCriticalSection(&g_lock);
}

/* -------------------------------------------------------- 登记控件与虚拟控件 */

BOOL ui_bridge_set_id(HWND ctl, const char *id)
{
    if (!ctl || !id) return FALSE;
    /* SetPropA 存的是指针：调用方必须传长期有效的字符串（字面量即可）。 */
    return SetPropA(ctl, "ui.id", (HANDLE) id);
}

BOOL ui_bridge_add_virtual(const char *id, const char *role, const char *name,
                           RECT rc, BOOL enabled, const char *text,
                           UI_VIRTUAL_CLICK on_click, void *user)
{
    if (!id || g_virtual_count >= UB_VIRTUAL_MAX) return FALSE;
    UB_VIRTUAL *v = &g_virtual[g_virtual_count++];
    memset(v, 0, sizeof(*v));
    ub_copy(v->id, UB_ID_LEN, id);
    ub_copy(v->role, 32, role && role[0] ? role : "custom");
    ub_copy(v->name, 256, name);
    ub_copy(v->text, 512, text);
    v->rect = rc;
    v->enabled = enabled;
    v->on_click = on_click;
    v->user = user;
    return TRUE;
}

/* -------------------------------------------------------------- 导出 UI 树 */

static cJSON *ub_node_json(HWND h, HWND main, int order)
{
    char id[UB_ID_LEN], cls[128], text[512];
    ub_effective_id(h, id, sizeof(id));
    ub_class_name(h, cls, sizeof(cls));
    ub_window_text(h, text, sizeof(text));

    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", id);
    cJSON_AddStringToObject(o, "role", ub_role_of(cls));
    cJSON_AddStringToObject(o, "class", cls);
    cJSON_AddStringToObject(o, "text", text);

    RECT r;
    GetWindowRect(h, &r);
    cJSON *rect = cJSON_CreateArray();
    cJSON_AddItemToArray(rect, cJSON_CreateNumber((double) r.left));
    cJSON_AddItemToArray(rect, cJSON_CreateNumber((double) r.top));
    cJSON_AddItemToArray(rect, cJSON_CreateNumber((double) r.right));
    cJSON_AddItemToArray(rect, cJSON_CreateNumber((double) r.bottom));
    cJSON_AddItemToObject(o, "rect", rect);

    cJSON_AddBoolToObject(o, "enabled", IsWindowEnabled(h) ? 1 : 0);
    cJSON_AddBoolToObject(o, "visible", IsWindowVisible(h) ? 1 : 0);
    cJSON_AddNumberToObject(o, "order", (double) order);

    HWND parent = GetParent(h);
    if (!parent || parent == main) {
        cJSON_AddStringToObject(o, "parent", "root");
    } else {
        char pid[UB_ID_LEN];
        ub_effective_id(parent, pid, sizeof(pid));
        cJSON_AddStringToObject(o, "parent", pid);
    }
    return o;
}

typedef struct {
    cJSON *arr;
    HWND   main;
    int    order;
} UB_WALK;

static BOOL CALLBACK ub_enum_cb(HWND h, LPARAM lp)
{
    UB_WALK *w = (UB_WALK *) lp;
    cJSON_AddItemToArray(w->arr, ub_node_json(h, w->main, w->order++));
    return TRUE;
}

static char *ub_tree_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "ui-tree");
    cJSON_AddNumberToObject(root, "ts", (double) GetTickCount());

    RECT mr;
    GetWindowRect(g_main, &mr);
    cJSON *mrect = cJSON_CreateArray();
    cJSON_AddItemToArray(mrect, cJSON_CreateNumber((double) mr.left));
    cJSON_AddItemToArray(mrect, cJSON_CreateNumber((double) mr.top));
    cJSON_AddItemToArray(mrect, cJSON_CreateNumber((double) mr.right));
    cJSON_AddItemToArray(mrect, cJSON_CreateNumber((double) mr.bottom));
    cJSON_AddItemToObject(root, "main_rect", mrect);

    cJSON *arr = cJSON_CreateArray();
    UB_WALK w;
    w.arr = arr;
    w.main = g_main;
    w.order = 0;
    EnumChildWindows(g_main, ub_enum_cb, (LPARAM) &w);

    for (int i = 0; i < g_virtual_count; i++) {
        UB_VIRTUAL *v = &g_virtual[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", v->id);
        cJSON_AddStringToObject(o, "role", v->role);
        cJSON_AddStringToObject(o, "class", "virtual");
        cJSON_AddStringToObject(o, "text", v->text);
        cJSON_AddStringToObject(o, "name", v->name);
        cJSON *rect = cJSON_CreateArray();
        cJSON_AddItemToArray(rect, cJSON_CreateNumber((double) v->rect.left));
        cJSON_AddItemToArray(rect, cJSON_CreateNumber((double) v->rect.top));
        cJSON_AddItemToArray(rect, cJSON_CreateNumber((double) v->rect.right));
        cJSON_AddItemToArray(rect, cJSON_CreateNumber((double) v->rect.bottom));
        cJSON_AddItemToObject(o, "rect", rect);
        cJSON_AddBoolToObject(o, "enabled", v->enabled);
        cJSON_AddBoolToObject(o, "visible", 1);
        cJSON_AddBoolToObject(o, "virtual", 1);
        cJSON_AddNumberToObject(o, "order", (double) w.order++);
        cJSON_AddStringToObject(o, "parent", "root");
        cJSON_AddItemToArray(arr, o);
    }

    cJSON_AddNumberToObject(root, "count", (double) cJSON_GetArraySize(arr));
    cJSON_AddItemToObject(root, "controls", arr);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

/* ---------------------------------------------------------- 定位与动作执行 */

typedef struct {
    const char *want;
    HWND        found;
} UB_FIND;

static BOOL CALLBACK ub_find_cb(HWND h, LPARAM lp)
{
    UB_FIND *f = (UB_FIND *) lp;
    char id[UB_ID_LEN];
    ub_effective_id(h, id, sizeof(id));
    if (strcmp(id, f->want) == 0) {
        f->found = h;
        return FALSE;
    }
    return TRUE;
}

static HWND ub_find_hwnd(const char *id)
{
    UB_FIND f;
    f.want = id;
    f.found = NULL;
    EnumChildWindows(g_main, ub_find_cb, (LPARAM) &f);
    if (f.found) return f.found;
    if (g_main) {
        char mid[UB_ID_LEN];
        ub_effective_id(g_main, mid, sizeof(mid));
        if (strcmp(mid, id) == 0) return g_main;
    }
    return NULL;
}

static UB_VIRTUAL *ub_find_virtual(const char *id)
{
    for (int i = 0; i < g_virtual_count; i++) {
        if (strcmp(g_virtual[i].id, id) == 0) return &g_virtual[i];
    }
    return NULL;
}

static void ub_notify_parent(HWND h, WORD code)
{
    HWND parent = GetParent(h);
    if (!parent) return;
    SendMessageW(parent, WM_COMMAND,
                 MAKEWPARAM((WORD) GetDlgCtrlID(h), code), (LPARAM) h);
}

static void ub_execute(const char *id, const char *action, const char *text,
                       int value, int *code, char *msg, int cap)
{
    *code = 0;
    snprintf(msg, (size_t) cap, "ok");

    UB_VIRTUAL *v = ub_find_virtual(id);
    if (v) {
        if (strcmp(action, "click") == 0) {
            if (!v->enabled) { *code = 409; snprintf(msg, (size_t) cap, "virtual control disabled"); return; }
            if (v->on_click) v->on_click(v->user);
            ui_bridge_log("virtual click %s", id);
            return;
        }
        *code = 400;
        snprintf(msg, (size_t) cap, "virtual control supports only click");
        return;
    }

    HWND h = ub_find_hwnd(id);
    if (!h) {
        *code = 404;
        snprintf(msg, (size_t) cap, "control not found: %s", id);
        return;
    }

    char cls[128];
    ub_class_name(h, cls, sizeof(cls));

    if (strcmp(action, "click") == 0) {
        if (!IsWindowEnabled(h)) {
            *code = 409;
            snprintf(msg, (size_t) cap, "control disabled");
            return;
        }
        if (strstr(cls, "Button")) {
            SendMessageW(h, BM_CLICK, 0, 0);          /* 会触发 BN_CLICKED 给父窗口 */
        } else {
            SetFocus(h);
            ub_notify_parent(h, BN_CLICKED);          /* 自定义控件的常规做法 */
        }
        ui_bridge_log("click %s (%s)", id, cls);
        return;
    }

    if (strcmp(action, "set_text") == 0) {
        WCHAR w[512];
        ub_utf8_to_wide(text ? text : "", w, (int) (sizeof(w) / sizeof(w[0])));
        SendMessageW(h, WM_SETTEXT, 0, (LPARAM) w);
        ui_bridge_log("set_text %s -> %s", id, text ? text : "");
        return;
    }

    if (strcmp(action, "focus") == 0) {
        SetFocus(h);
        return;
    }

    if (strcmp(action, "select") == 0) {
        if (strstr(cls, "ComboBox")) {
            SendMessageW(h, CB_SETCURSEL, (WPARAM) value, 0);
            ub_notify_parent(h, CBN_SELCHANGE);
            ui_bridge_log("select %s index=%d", id, value);
            return;
        }
        if (strstr(cls, "SysListView")) {
            SendMessageW(h, LVM_SETITEMSTATE, (WPARAM) value, 0);
            ub_notify_parent(h, LVN_ITEMCHANGED);
            return;
        }
        *code = 400;
        snprintf(msg, (size_t) cap, "select unsupported for %s", cls);
        return;
    }

    if (strcmp(action, "command") == 0) {
        /* 触发菜单 / 快捷键命令号（value 为命令 ID） */
        SendMessageW(g_main, WM_COMMAND, MAKEWPARAM((WORD) value, 0), 0);
        ui_bridge_log("command %d", value);
        return;
    }

    *code = 400;
    snprintf(msg, (size_t) cap, "unknown action: %s", action);
}

/* UI 线程消费动作请求（由 ui_bridge_handle_message 调用）。 */
BOOL ui_bridge_handle_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg != UI_BRIDGE_WM_ACTION) return FALSE;

    char id[UB_ID_LEN] = {0}, action[32] = {0}, text[512] = {0};
    int value = 0, have = 0, code = 0;
    char reply[512] = {0};

    EnterCriticalSection(&g_lock);
    if (g_req_pending) {
        ub_copy(id, UB_ID_LEN, g_req.id);
        ub_copy(action, 32, g_req.action);
        ub_copy(text, 512, g_req.text);
        value = g_req.value;
        have = 1;
    }
    LeaveCriticalSection(&g_lock);

    if (have) {
        ub_execute(id, action[0] ? action : "click", text, value, &code, reply, sizeof(reply));
        if (code == 0) {
            InterlockedIncrement(&g_action_count);
            EnterCriticalSection(&g_lock);
            snprintf(g_last_action, sizeof(g_last_action), "%s:%s", action, id);
            LeaveCriticalSection(&g_lock);
        }
    } else {
        code = 400;
        ub_copy(reply, sizeof(reply), "no pending request");
    }

    EnterCriticalSection(&g_lock);
    g_res_code = code;
    ub_copy(g_res_msg, sizeof(g_res_msg), reply);
    LeaveCriticalSection(&g_lock);

    SetEvent(g_done_event);
    (void) hwnd; (void) wp; (void) lp;
    return TRUE;
}

/* ---------------------------------------------------------------- 业务状态 */

static char *ub_state_json(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "state");
    cJSON_AddNumberToObject(o, "ts", (double) GetTickCount());
    cJSON_AddNumberToObject(o, "action_count", (double) g_action_count);

    EnterCriticalSection(&g_lock);
    cJSON_AddStringToObject(o, "last_action", g_last_action);
    LeaveCriticalSection(&g_lock);

    HWND focus = GetFocus();
    if (focus) {
        char fid[UB_ID_LEN];
        ub_effective_id(focus, fid, sizeof(fid));
        cJSON_AddStringToObject(o, "focus_id", fid);
    } else {
        cJSON_AddStringToObject(o, "focus_id", "");
    }

    if (g_state_cb) {
        char buf[4096];
        buf[0] = '\0';
        g_state_cb(buf, (int) sizeof(buf), g_state_user);
        if (buf[0]) {
            cJSON *state = cJSON_Parse(buf);
            if (state) {
                cJSON_AddItemToObject(o, "state", state);
            } else {
                cJSON_AddStringToObject(o, "state_raw", buf);
            }
        }
    }

    char *out = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return out;
}

static char *ub_logs_json(unsigned since)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "logs");
    cJSON *arr = cJSON_CreateArray();

    EnterCriticalSection(&g_lock);
    unsigned first = (g_log_seq > UB_LOG_MAX) ? g_log_seq - UB_LOG_MAX : 0;
    for (unsigned s = first; s < g_log_seq; s++) {
        if (s < since) continue;
        UB_LOG_ENTRY *e = &g_logs[s % UB_LOG_MAX];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "seq", (double) e->seq);
        cJSON_AddNumberToObject(o, "ms", (double) e->ms);
        cJSON_AddStringToObject(o, "text", e->text);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddNumberToObject(root, "next_since", (double) g_log_seq);
    LeaveCriticalSection(&g_lock);

    cJSON_AddItemToObject(root, "entries", arr);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

/* -------------------------------------------------------------------- HTTP */

static int ub_uri_is(const struct mg_str *uri, const char *s)
{
    size_t n = strlen(s);
    return uri && (size_t) uri->len == n && memcmp(uri->buf, s, n) == 0;
}

static int ub_token_ok(struct mg_http_message *hm)
{
    /* S-02：必须带令牌；兼容 Header 与 ?token= */
    char qtok[64] = {0};
    struct mg_str *h;
    if (!g_token[0]) return 0;
    h = mg_http_get_header(hm, "X-Ui-Token");
    if (h && (size_t)h->len == strlen(g_token) &&
        memcmp(h->buf, g_token, (size_t)h->len) == 0)
        return 1;
    if (mg_http_get_var(&hm->query, "token", qtok, sizeof(qtok)) > 0 &&
        strcmp(qtok, g_token) == 0)
        return 1;
    return 0;
}

static void ub_send_json(struct mg_connection *c, int status, char *json)
{
    /* 仅本机环回，不加 CORS * */
    mg_http_reply(c, status,
                  "Content-Type: application/json\r\n",
                  "%s", json ? json : "{}");
    free(json); /* cJSON_Print 与 _strdup 均走 CRT malloc */
}

static int ub_invoke(const char *id, const char *action, const char *text, int value,
                     char *msg, int cap)
{
    EnterCriticalSection(&g_lock);
    if (g_req_pending) {
        LeaveCriticalSection(&g_lock);
        snprintf(msg, (size_t) cap, "busy: another action is in flight");
        return 429;
    }
    ub_copy(g_req.id, UB_ID_LEN, id);
    ub_copy(g_req.action, 32, action);
    ub_copy(g_req.text, 512, text);
    g_req.value = value;
    g_req_pending = 1;
    g_res_code = 0;
    g_res_msg[0] = '\0';
    LeaveCriticalSection(&g_lock);

    ResetEvent(g_done_event);
    if (!PostMessageW(g_main, UI_BRIDGE_WM_ACTION, 0, 0)) {
        EnterCriticalSection(&g_lock);
        g_req_pending = 0;
        LeaveCriticalSection(&g_lock);
        snprintf(msg, (size_t) cap, "post message failed");
        return 500;
    }

    int code;
    if (WaitForSingleObject(g_done_event, UB_WAIT_MS) != WAIT_OBJECT_0) {
        EnterCriticalSection(&g_lock);
        g_req_pending = 0;
        LeaveCriticalSection(&g_lock);
        snprintf(msg, (size_t) cap, "timeout: UI thread did not run the action");
        return 504;
    }

    EnterCriticalSection(&g_lock);
    g_req_pending = 0;
    code = g_res_code;
    ub_copy(msg, cap, g_res_msg);
    LeaveCriticalSection(&g_lock);
    return code;
}

static void ub_handle_action(struct mg_connection *c, struct mg_http_message *hm)
{
    char body[2048];
    size_t n = (size_t) hm->body.len;
    if (n >= sizeof(body)) {
        ub_send_json(c, 413, NULL);
        return;
    }
    memcpy(body, hm->body.buf, n);
    body[n] = '\0';

    cJSON *req = cJSON_Parse(body);
    if (!req) {
        ub_send_json(c, 400, NULL);
        return;
    }

    const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(req, "id"));
    const char *act = cJSON_GetStringValue(cJSON_GetObjectItem(req, "action"));
    const char *text = cJSON_GetStringValue(cJSON_GetObjectItem(req, "text"));
    cJSON *val = cJSON_GetObjectItem(req, "value");

    if (!id || !id[0]) { cJSON_Delete(req); ub_send_json(c, 400, NULL); return; }

    char msg[512] = {0};
    int code = ub_invoke(id, act && act[0] ? act : "click", text ? text : "",
                         val ? (int) val->valuedouble : 0, msg, (int) sizeof(msg));

    cJSON *res = cJSON_CreateObject();
    cJSON_AddBoolToObject(res, "ok", code == 0 ? 1 : 0);
    cJSON_AddNumberToObject(res, "code", (double) code);
    cJSON_AddStringToObject(res, "id", id);
    cJSON_AddStringToObject(res, "action", act && act[0] ? act : "click");
    cJSON_AddStringToObject(res, "message", msg);
    char *json = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    cJSON_Delete(req);
    ub_send_json(c, code == 0 ? 200 : (code >= 400 ? code : 400), json);
}

static void ub_http(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev != MG_EV_HTTP_MSG) return;
    struct mg_http_message *hm = (struct mg_http_message *) ev_data;

    if (ub_uri_is(&hm->method, "OPTIONS")) {
        mg_http_reply(c, 204, "", "");
        return;
    }
    /* /ping 可无令牌探活；其余全部鉴权 */
    if (!ub_uri_is(&hm->uri, "/ping") && !ub_token_ok(hm)) {
        ub_send_json(c, 401, NULL);
        return;
    }

    if (ub_uri_is(&hm->uri, "/ui-tree")) {
        ub_send_json(c, 200, ub_tree_json());
        return;
    }
    if (ub_uri_is(&hm->uri, "/state")) {
        ub_send_json(c, 200, ub_state_json());
        return;
    }
    if (ub_uri_is(&hm->uri, "/logs")) {
        char q[32] = {0};
        long since = 0;
        if (mg_http_get_var(&hm->query, "since", q, sizeof(q)) > 0) since = atol(q);
        ub_send_json(c, 200, ub_logs_json((unsigned) (since < 0 ? 0 : since)));
        return;
    }
    if (ub_uri_is(&hm->uri, "/action")) {
        if (!ub_uri_is(&hm->method, "POST")) {
            ub_send_json(c, 405, NULL);
            return;
        }
        ub_handle_action(c, hm);
        return;
    }
    if (ub_uri_is(&hm->uri, "/ping")) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", 1);
        cJSON_AddStringToObject(o, "service", "win_ui_bridge");
        cJSON_AddStringToObject(o, "agent", "yt_agent");
        char *json = cJSON_PrintUnformatted(o);
        cJSON_Delete(o);
        ub_send_json(c, 200, json);
        return;
    }
    /* Agent 控制面（ssh-bridge 风格） */
    {
        char method[16] = {0}, uri[256] = {0}, query[512] = {0}, body[8192] = {0};
        int st = 0, is_text = 0;
        char *out = NULL;
        size_t blen = (size_t)hm->body.len;
        if ((size_t)hm->method.len < sizeof(method))
            memcpy(method, hm->method.buf, (size_t)hm->method.len);
        if ((size_t)hm->uri.len < sizeof(uri))
            memcpy(uri, hm->uri.buf, (size_t)hm->uri.len);
        if ((size_t)hm->query.len < sizeof(query))
            memcpy(query, hm->query.buf, (size_t)hm->query.len);
        if (blen >= sizeof(body)) blen = sizeof(body) - 1;
        if (blen) memcpy(body, hm->body.buf, blen);
        if (yt_agent_try(method, uri, query, body, &st, &out, &is_text)) {
            if (is_text) {
                mg_http_reply(c, st, "Content-Type: text/plain; charset=utf-8\r\n",
                              "%s", out ? out : "");
                free(out);
            } else {
                ub_send_json(c, st, out);
            }
            return;
        }
    }
    ub_send_json(c, 404, NULL);
}

const char *ui_bridge_token(void)
{
    return g_token;
}

/* ------------------------------------------------------------------ 生命周期 */

static DWORD WINAPI ub_http_thread(LPVOID param)
{
    struct mg_mgr mgr;
    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d", (int) (intptr_t) param);

    mg_mgr_init(&mgr);
    if (mg_http_listen(&mgr, url, ub_http, NULL) == NULL) {
        ui_bridge_log("bridge: listen failed on %s", url);
    } else {
        ui_bridge_log("bridge: listening on %s", url);
    }

    while (g_running) mg_mgr_poll(&mgr, 50);

    mg_mgr_free(&mgr);
    return 0;
}

void ui_bridge_set_token(const char *token)
{
    ub_copy(g_token, sizeof(g_token), token);
}

void ui_bridge_set_state_provider(UI_STATE_CB cb, void *user)
{
    g_state_cb = cb;
    g_state_user = user;
}

static void ub_gen_token(void)
{
    unsigned i;
    static const char hex[] = "0123456789abcdef";
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    srand((unsigned)(q.LowPart ^ GetCurrentProcessId()));
    for (i = 0; i < 32; i++)
        g_token[i] = hex[rand() % 16];
    g_token[32] = 0;
}

int ui_bridge_start(HWND hwnd_main, int port)
{
    if (g_inited) return 1;
    g_main = hwnd_main;
    InitializeCriticalSection(&g_lock);
    g_done_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!g_done_event) return 0;
    if (!g_token[0]) ub_gen_token();
    g_running = 1;
    g_inited = 1;
    ui_bridge_log("bridge: start on port %d token=%s", port, g_token);
    {
        /* 写入 config\\ui_bridge.token，便于本地测试脚本读取 */
        wchar_t path[MAX_PATH];
        char dir8[MAX_PATH], tokpath[MAX_PATH];
        FILE *fp;
        if (GetModuleFileNameA(NULL, dir8, MAX_PATH)) {
            char *slash = strrchr(dir8, '\\');
            if (slash) *slash = 0;
            snprintf(tokpath, sizeof(tokpath), "%s\\config\\ui_bridge.token", dir8);
            CreateDirectoryA(dir8, NULL);
            {
                char cfg[MAX_PATH];
                snprintf(cfg, sizeof(cfg), "%s\\config", dir8);
                CreateDirectoryA(cfg, NULL);
            }
            fp = fopen(tokpath, "wb");
            if (fp) { fputs(g_token, fp); fclose(fp); }
        }
        (void)path;
    }

    g_http_thr = CreateThread(NULL, 0, ub_http_thread, (LPVOID) (intptr_t) port, 0, NULL);
    if (!g_http_thr) {
        g_inited = 0;
        g_running = 0;
        return 0;
    }
    return 1;
}

void ui_bridge_stop(void)
{
    if (!g_inited) return;
    g_running = 0;
    if (g_http_thr) {
        WaitForSingleObject(g_http_thr, 2000);
        CloseHandle(g_http_thr);
        g_http_thr = NULL;
    }
    if (g_done_event) CloseHandle(g_done_event);
    g_done_event = NULL;
    DeleteCriticalSection(&g_lock);
    g_inited = 0;
    g_token[0] = 0;
}
