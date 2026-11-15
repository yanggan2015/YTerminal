#include "yt_core.h"
#include "app.h"
#include "win_ui_bridge.h"
#include "yt_agent.h"
#include <shellapi.h>
#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

/* SecureCRT-like chrome metrics */
#define TB_H   36
#define CB_H   30
#define BB_H   32
#define ST_H   24
#define CAP_H  26
#define TREE_W 230
#define SFTP_W 250

#define COL_CHROME RGB(245, 245, 247)
#define COL_CAP    RGB(232, 232, 236)
#define COL_LINE   RGB(196, 196, 200)
#define COL_TERM_EDGE RGB(60, 60, 60)

#define ID_TAB 1001
#define ID_TREE 1002
#define ID_SFTP 1003
#define ID_STATUS 1004
#define ID_CMB_KIND 1020
#define ID_HOST 1021
#define ID_PORT 1022
#define ID_USER 1023
#define ID_PASS 1024
#define ID_COM 1025
#define ID_BAUD 1026
#define ID_JUMP 1027
#define ID_FWD 1028
#define ID_KEY 1029
#define ID_SHELL 1030
#define ID_FIND 1106
#define ID_SFTP_PATH 1107
#define ID_QUICK 1108
#define ID_BTN_QUICK 1109
#define ID_SM_CAP 1201
#define ID_SFTP_CAP 1202
#define ID_BTN_SFTP_REF 1102
#define ID_BTN_SFTP_UP 1103
#define ID_BTN_SFTP_DN 1104

#define ID_TB_CONNECT 1301
#define ID_TB_QUICK   1302
#define ID_TB_LOCAL   1303
#define ID_TB_DISC    1304
#define ID_TB_RECONN  1305
#define ID_TB_SYNC    1306
#define ID_TB_PROP    1307
#define ID_TB_SFTP    1308
#define ID_TB_FINDGO  1309
#define ID_CB_GO      1310
#define ID_BB0        1400

#define ID_TERM_BASE 2000

/* —— 文件 —— */
#define IDM_QUICK_CONN 4222
#define IDM_NEW_LOCAL 4223
#define IDM_NEW_CMD 4301
#define IDM_NEW_ADMIN 4302
#define IDM_CONNECT 4303
#define IDM_DISCONNECT 4304
#define IDM_RECONNECT 4208
#define IDM_CLONE_TAB 4305
#define IDM_CLOSE_TAB 4306
#define IDM_TAB_CLOSE_OTHER 4209
#define IDM_CLOSE_ALL 4307
#define IDM_SAVE 4001
#define IDM_LOAD 4002
#define IDM_ADD_TREE 4107
#define IDM_EXPORT_SESS 4308
#define IDM_IMPORT_SESS 4309
#define IDM_LAYOUT_SAVE 4206
#define IDM_LAYOUT_LOAD 4207
#define IDM_OPEN_CONFIG 4310
#define IDM_OPEN_LOGDIR 4311
#define IDM_EXIT 4106
/* —— 编辑 —— */
#define IDM_EDIT_COPY 4312
#define IDM_EDIT_PASTE 4313
#define IDM_EDIT_ALL 4314
#define IDM_EDIT_CLR 4315
#define IDM_FIND 4100
#define IDM_FIND_NEXT 4316
#define IDM_PASTE_0 4317
#define IDM_PASTE_10 4318
#define IDM_PASTE_50 4319
#define IDM_PASTE_100 4320
#define IDM_SYNC_TOGGLE 4321
/* —— 查看 —— */
#define IDM_VIEW_SM 4224
#define IDM_VIEW_BB 4225
#define IDM_VIEW_SFTP 4226
#define IDM_VIEW_TB 4322
#define IDM_VIEW_CB 4323
#define IDM_VIEW_ST 4324
#define IDM_SPLIT 4104
#define IDM_SPLIT_OFF 4325
#define IDM_SPLIT_H 4326
#define IDM_SPLIT_V 4327
#define IDM_FULLSCREEN 4328
#define IDM_ONTOP 4329
#define IDM_OPACITY100 4330
#define IDM_OPACITY90 4331
#define IDM_OPACITY80 4332
#define IDM_ZOOM_IN 4333
#define IDM_ZOOM_OUT 4334
#define IDM_ZOOM_RESET 4335
#define IDM_THEME 4219
#define IDM_THEME_DARK 4336
/* —— 会话 —— */
#define IDM_PROP 4205
#define IDM_LOG 4003
#define IDM_LOCK 4337
#define IDM_TAB_COLOR 4210
#define IDM_SEND_STR 4338
#define IDM_TREE_CONN 4201
#define IDM_TREE_EDIT 4202
#define IDM_TREE_DEL  4203
#define IDM_TREE_CLONE 4204
/* —— 传输 —— */
#define IDM_ZRX 4101
#define IDM_ZTX 4102
#define IDM_SFTP_REF 4339
#define IDM_SFTP_UP 4340
#define IDM_SFTP_DN 4341
#define IDM_DUMP 4217
#define IDM_LOCAL_FILES 4342
/* —— 脚本 —— */
#define IDM_MACRO_REC 4211
#define IDM_MACRO_STOP 4212
#define IDM_MACRO_RUN 4213
#define IDM_MACRO_SAVE 4214
#define IDM_SCRIPT 4215
#define IDM_COMPLETE 4216
/* —— 工具 —— */
#define IDM_X11 4103
#define IDM_UNIX 4218
#define IDM_COM_ENUM 4220
#define IDM_STATS 4221
#define IDM_OPEN_KEYS 4343
#define IDM_OPEN_RUNTIME 4344
#define IDM_CRED_HINT 4345
#define IDM_ABOUT 4105
#define IDM_HOTKEYS 4346
/* —— 窗口 —— */
#define IDM_TAB_NEXT 4347
#define IDM_TAB_PREV 4348
#define IDM_NEW_WINDOW 4349
#define IDT_STATS 5001

#define BB_N YT_BB_N

static const wchar_t *k_bb_cmd[BB_N] = {
    L"ls -la", L"pwd", L"top -bn1 | head", L"df -h", L"free -m", L"uname -a"
};
static void refresh_sftp(void);





static HWND mk_tb(HWND p, int id, const wchar_t *txt)
{
    HWND h = CreateWindowW(L"BUTTON", txt, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                           0, 0, 70, 26, p, (HMENU)(INT_PTR)id, g.hi, 0);
    SendMessageW(h, WM_SETFONT, (WPARAM)g.font_ui, TRUE);
    return h;
}

static void refresh_status_bar(void)
{
    int cur = yt_app_cur_tab(), row = 0, col = 0, lines = 0;
    YtSession *s;
    wchar_t buf[360];
    const wchar_t *proto = L"-", *st = L"空闲";
    if (cur < 0 || cur >= g.ntabs) {
        SetWindowTextW(g.status, L"  就绪  |  UTF-8  |  YTerminal " YT_VERSION);
        return;
    }
    s = g.sess[cur];
    term_get_cursor(g.terms[cur], &row, &col, &lines);
    if (s->kind == YT_KIND_LOCAL) proto = L"Local";
    else if (s->kind == YT_KIND_SSH) proto = L"SSH2";
    else if (s->kind == YT_KIND_SERIAL) proto = L"Serial";
    else if (s->kind == YT_KIND_TELNET) proto = L"Telnet";
    if (s->state == YT_ST_CONNECTED) st = L"已连接";
    else if (s->state == YT_ST_CONNECTING) st = L"连接中";
    else if (s->state == YT_ST_CLOSED) st = L"已断开";
    else if (s->state == YT_ST_ERROR) st = L"错误";
    if (g.stats_on) {
        wchar_t mon[128];
        yt_sys_stats(mon, 128);
        swprintf(buf, 360, L"  %ls  |  %ls  |  UTF-8  |  %d,%d  |  %ls  |  %ls",
                 st, proto, row, col, s->title, mon);
    } else {
        swprintf(buf, 360, L"  %ls  |  %ls  |  UTF-8  |  行 %d  列 %d  |  %ls  |  同步 %ls  |  缓冲 %d",
                 st, proto, row, col, s->title, s->sync_input ? L"开" : L"关", lines);
    }
    SetWindowTextW(g.status, buf);
}

static void sync_hook(YtSession *from, const char *data, int len)
{
    int i, from_i = -1;
    for (i = 0; i < g.ntabs; i++) if (g.sess[i] == from) { from_i = i; break; }
    yt_sync_broadcast(g.sess, g.ntabs, from_i, data, len);
}

static void apply_opacity(int pct)
{
    LONG_PTR ex;
    if (pct < 80) pct = 80;
    if (pct > 100) pct = 100;
    g.opacity = (255 * pct) / 100;
    ex = GetWindowLongPtrW(g.hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(g.hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    SetLayeredWindowAttributes(g.hwnd, 0, (BYTE)g.opacity, LWA_ALPHA);
}

static void save_window_cfg(void)
{
    YtWindowCfg c = {0};
    WINDOWPLACEMENT wp = { sizeof(wp) };
    GetWindowPlacement(g.hwnd, &wp);
    c.x = wp.rcNormalPosition.left;
    c.y = wp.rcNormalPosition.top;
    c.w = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
    c.h = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
    c.maximized = (wp.showCmd == SW_SHOWMAXIMIZED);
    c.sm_visible = g.sm_visible;
    c.bb_visible = g.bb_visible;
    c.sftp_visible = g.sftp_visible;
    c.split = g.split_mode;
    c.theme = g.theme;
    c.font_px = g.font_px > 0 ? g.font_px : 15;
    c.opacity = g.opacity > 0 ? g.opacity : 255;
    c.kind = yt_app_kind_sel();
    c.stats = g.stats_on;
    c.paste_delay_ms = yt_get_paste_delay();
    yt_window_save(&c);
    yt_profile_save(g.profiles, g.tree_n);
}

static void apply_font_all(int px)
{
    int i;
    g.font_px = px;
    for (i = 0; i < g.ntabs; i++) term_set_font_px(g.terms[i], px);
}

static void toggle_fullscreen(HWND hwnd)
{
    if (!g.fullscreen) {
        g.fs_wp.length = sizeof(g.fs_wp);
        GetWindowPlacement(hwnd, &g.fs_wp);
        SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN);
        {
            HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfoW(mon, &mi);
            SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED);
        }
        g.fullscreen = TRUE;
    } else {
        SetWindowLongPtrW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN);
        SetWindowPlacement(hwnd, &g.fs_wp);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        g.fullscreen = FALSE;
    }
    yt_app_layout();
}

void yt_app_layout(void)
{
    RECT rc;
    int top, tree_w, sftp_w, mid_x, mid_w, term_top, term_h, bb_y, i, cur;
    int tab_h = 28;
    GetClientRect(g.hwnd, &rc);
    cur = yt_app_cur_tab();
    tree_w = g.sm_visible ? TREE_W : 0;
    sftp_w = g.sftp_visible ? SFTP_W : 0;

    /* Toolbar */
    if (g.tb_visible) {
        int x = 8, y = 5, i;
        for (i = 0; i < 9; i++) ShowWindow(g.tb[i], SW_SHOW);
        ShowWindow(g.find, SW_SHOW);
        MoveWindow(g.tb[0], x, y, 72, 26, TRUE); x += 76;
        MoveWindow(g.tb[1], x, y, 96, 26, TRUE); x += 100;
        MoveWindow(g.tb[2], x, y, 72, 26, TRUE); x += 76;
        MoveWindow(g.tb[3], x, y, 72, 26, TRUE); x += 76;
        MoveWindow(g.tb[4], x, y, 72, 26, TRUE); x += 76;
        MoveWindow(g.tb[5], x, y, 72, 26, TRUE); x += 76;
        MoveWindow(g.tb[6], x, y, 72, 26, TRUE); x += 76;
        MoveWindow(g.tb[7], x, y, 80, 26, TRUE); x += 92;
        MoveWindow(g.find, x, y, 150, 26, TRUE); x += 154;
        MoveWindow(g.tb[8], x, y, 40, 26, TRUE);
    } else {
        int i;
        for (i = 0; i < 9; i++) ShowWindow(g.tb[i], SW_HIDE);
        ShowWindow(g.find, SW_HIDE);
    }

    /* Connect bar */
    top = g.tb_visible ? TB_H : 0;
    if (g.cb_visible) {
        MoveWindow(g.cmb, 8, top + 2, 78, 200, TRUE);
        MoveWindow(g.host, 92, top + 2, 200, 24, TRUE);
        MoveWindow(g.port, 298, top + 2, 52, 24, TRUE);
        MoveWindow(g.user, 356, top + 2, 80, 24, TRUE);
        MoveWindow(g.pass, 442, top + 2, 80, 24, TRUE);
        MoveWindow(g.com, 92, top + 2, 90, 24, TRUE);
        MoveWindow(g.baud, 188, top + 2, 100, 24, TRUE);
        MoveWindow(g.shell, 92, top + 2, 140, 200, TRUE);
        MoveWindow(g.cb_go, 570, top + 1, 72, 26, TRUE);
        ShowWindow(g.cmb, SW_SHOW);
        ShowWindow(g.cb_go, SW_SHOW);
        {
            int k = (int)SendMessageW(g.cmb, CB_GETCURSEL, 0, 0);
            int ssh = (k == 1), ser = (k == 2), rem = (k == 3), loc = (k == 0);
            ShowWindow(g.host, (ssh || rem) ? SW_SHOW : SW_HIDE);
            ShowWindow(g.port, (ssh || rem) ? SW_SHOW : SW_HIDE);
            ShowWindow(g.user, (ssh || rem) ? SW_SHOW : SW_HIDE);
            ShowWindow(g.pass, (ssh || rem) ? SW_SHOW : SW_HIDE);
            ShowWindow(g.com, ser ? SW_SHOW : SW_HIDE);
            ShowWindow(g.baud, ser ? SW_SHOW : SW_HIDE);
            ShowWindow(g.shell, loc ? SW_SHOW : SW_HIDE);
        }
        top += CB_H;
    } else {
        ShowWindow(g.cmb, SW_HIDE); ShowWindow(g.cb_go, SW_HIDE);
        ShowWindow(g.host, SW_HIDE); ShowWindow(g.port, SW_HIDE);
        ShowWindow(g.user, SW_HIDE); ShowWindow(g.pass, SW_HIDE);
        ShowWindow(g.com, SW_HIDE); ShowWindow(g.baud, SW_HIDE);
        ShowWindow(g.shell, SW_HIDE);
    }
    MoveWindow(g.key, -400, 0, 10, 10, TRUE);
    MoveWindow(g.jump, -400, 0, 10, 10, TRUE);
    MoveWindow(g.fwd, -400, 0, 10, 10, TRUE);
    ShowWindow(g.key, SW_HIDE); ShowWindow(g.jump, SW_HIDE); ShowWindow(g.fwd, SW_HIDE);
    mid_x = tree_w;
    mid_w = rc.right - tree_w - sftp_w;
    if (mid_w < 120) mid_w = 120;

    if (g.sm_visible) {
        MoveWindow(g.sm_cap, 0, top, tree_w, CAP_H, TRUE);
        MoveWindow(g.tree, 0, top + CAP_H, tree_w,
                   rc.bottom - top - CAP_H - ST_H - (g.bb_visible ? BB_H : 0), TRUE);
        ShowWindow(g.sm_cap, SW_SHOW);
        ShowWindow(g.tree, SW_SHOW);
    } else {
        ShowWindow(g.sm_cap, SW_HIDE);
        ShowWindow(g.tree, SW_HIDE);
    }

    MoveWindow(g.tab, mid_x, top, mid_w, tab_h, TRUE);
    term_top = top + tab_h;
    bb_y = rc.bottom - (g.st_visible ? ST_H : 0) - (g.bb_visible ? BB_H : 0);
    term_h = bb_y - term_top;
    if (term_h < 40) term_h = 40;

    if (cur < 0 && g.ntabs > 0) {
        SendMessageW(g.tab, TCM_SETCURSEL, 0, 0);
        cur = 0;
    }
    for (i = 0; i < g.ntabs; i++) {
        int x = mid_x + 1, y = term_top, w = mid_w - 2, h = term_h;
        int peer = (cur > 0) ? cur - 1 : 1;
        BOOL show = (i == cur);
        if (g.split_mode && g.ntabs >= 2 && (i == cur || i == peer)) {
            show = TRUE;
            if (g.split_mode == 1) {
                if (i == cur) { x += w / 2; w /= 2; } else w /= 2;
            } else {
                if (i == cur) { y += h / 2; h /= 2; } else h /= 2;
            }
        }
        if (!show) {
            /* S-32：关闭分屏后隐藏并缩到 0，避免与当前标签重叠 */
            ShowWindow(g.terms[i], SW_HIDE);
            MoveWindow(g.terms[i], 0, 0, 0, 0, TRUE);
            continue;
        }
        MoveWindow(g.terms[i], x, y, w, h, TRUE);
        ShowWindow(g.terms[i], SW_SHOW);
        SetWindowPos(g.terms[i], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    }

    if (g.sftp_visible) {
        int sx = rc.right - sftp_w;
        int sh = rc.bottom - top - ST_H - (g.bb_visible ? BB_H : 0);
        MoveWindow(g.sftp_cap, sx, top, sftp_w, CAP_H, TRUE);
        MoveWindow(g.sftp_path, sx + 6, top + CAP_H + 4, sftp_w - 12, 22, TRUE);
        MoveWindow(g.sftp, sx + 4, top + CAP_H + 30, sftp_w - 8, sh - CAP_H - 66, TRUE);
        MoveWindow(g.btn_sref, sx + 6, top + sh - 30, 70, 24, TRUE);
        MoveWindow(g.btn_sup, sx + 82, top + sh - 30, 70, 24, TRUE);
        MoveWindow(g.btn_sdn, sx + 158, top + sh - 30, 70, 24, TRUE);
        ShowWindow(g.sftp_cap, SW_SHOW);
        ShowWindow(g.sftp_path, SW_SHOW);
        ShowWindow(g.sftp, SW_SHOW);
        ShowWindow(g.btn_sref, SW_SHOW);
        ShowWindow(g.btn_sup, SW_SHOW);
        ShowWindow(g.btn_sdn, SW_SHOW);
    } else {
        ShowWindow(g.sftp_cap, SW_HIDE);
        ShowWindow(g.sftp_path, SW_HIDE);
        ShowWindow(g.sftp, SW_HIDE);
        ShowWindow(g.btn_sref, SW_HIDE);
        ShowWindow(g.btn_sup, SW_HIDE);
        ShowWindow(g.btn_sdn, SW_HIDE);
    }

    if (g.bb_visible) {
        int x = 8, y = bb_y + 3;
        for (i = 0; i < BB_N; i++) {
            MoveWindow(g.bb[i], x, y, 88, 24, TRUE);
            ShowWindow(g.bb[i], SW_SHOW);
            x += 92;
        }
        MoveWindow(g.quick, x + 8, y, rc.right - x - 100, 24, TRUE);
        MoveWindow(g.btn_quick, rc.right - 84, y, 72, 24, TRUE);
        ShowWindow(g.quick, SW_SHOW);
        ShowWindow(g.btn_quick, SW_SHOW);
    } else {
        for (i = 0; i < BB_N; i++) ShowWindow(g.bb[i], SW_HIDE);
        ShowWindow(g.quick, SW_HIDE);
        ShowWindow(g.btn_quick, SW_HIDE);
    }

    if (g.st_visible) {
        MoveWindow(g.status, 0, rc.bottom - ST_H, rc.right, ST_H, TRUE);
        ShowWindow(g.status, SW_SHOW);
        refresh_status_bar();
    } else {
        ShowWindow(g.status, SW_HIDE);
    }
    /* 焦点延后设置，避免 TCN_SELCHANGE 内 SetFocus 重入卡死 */
    if (cur >= 0 && cur < g.ntabs)
        PostMessageW(g.hwnd, WM_YT_FOCUS_TERM, (WPARAM)cur, 0);
    InvalidateRect(g.hwnd, NULL, FALSE);
}

static void paint_chrome(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc, r;
    HBRUSH line = CreateSolidBrush(COL_LINE);
    HBRUSH term_bg = CreateSolidBrush(RGB(12, 12, 12));
    int top = (g.tb_visible ? TB_H : 0) + (g.cb_visible ? CB_H : 0);
    int tree_w = g.sm_visible ? TREE_W : 0;
    int sftp_w = g.sftp_visible ? SFTP_W : 0;
    int bb = g.bb_visible ? BB_H : 0;
    int sth = g.st_visible ? ST_H : 0;
    GetClientRect(hwnd, &rc);
    /* top chrome */
    r = rc; r.bottom = top; FillRect(hdc, &r, g.brush_chrome);
    /* left session manager chrome */
    if (tree_w) {
        r.left = 0; r.right = tree_w; r.top = top; r.bottom = rc.bottom - sth - bb;
        FillRect(hdc, &r, g.brush_white);
    }
    r.left = tree_w; r.right = rc.right - sftp_w; r.top = top; r.bottom = rc.bottom - sth - bb;
    FillRect(hdc, &r, term_bg);
    if (bb) {
        r.left = 0; r.right = rc.right; r.top = rc.bottom - sth - bb; r.bottom = rc.bottom - sth;
        FillRect(hdc, &r, g.brush_chrome);
    }
    if (sth) {
        r.left = 0; r.right = rc.right; r.top = rc.bottom - sth; r.bottom = rc.bottom;
        FillRect(hdc, &r, g.brush_status);
    }
    if (g.tb_visible) { r.left = 0; r.right = rc.right; r.top = TB_H - 1; r.bottom = TB_H; FillRect(hdc, &r, line); }
    if (g.cb_visible) { r.top = top - 1; r.bottom = top; FillRect(hdc, &r, line); }
    if (bb) { r.top = rc.bottom - sth - bb; r.bottom = r.top + 1; FillRect(hdc, &r, line); }
    if (sth) { r.top = rc.bottom - sth; r.bottom = r.top + 1; FillRect(hdc, &r, line); }
    if (tree_w) {
        r.left = tree_w - 1; r.right = tree_w; r.top = top; r.bottom = rc.bottom - sth - bb;
        FillRect(hdc, &r, line);
    }
    DeleteObject(line);
    DeleteObject(term_bg);
    EndPaint(hwnd, &ps);
}

/* 切到 SSH 标签时绝不能在 UI 线程同步 listdir（会与 ssh_reader 抢锁/堵网络 → 未响应） */
static volatile LONG g_sftp_busy;
static volatile LONG g_sftp_pending;

typedef struct {
    YtSession *s;
    HWND hwnd;
    char cwd[YT_PATH_MAX];
} SftpListJob;

typedef struct {
    YtSession *s;
    int ok;
    char *list;
} SftpDoneMsg;

static DWORD WINAPI sftp_list_thr(LPVOID arg)
{
    SftpListJob *j = (SftpListJob *)arg;
    SftpDoneMsg *done = (SftpDoneMsg *)calloc(1, sizeof(*done));
    char *list = (char *)malloc(16384);
    int ok = 0;
    if (list) {
        list[0] = 0;
        if (j->s && !j->s->dead) {
            if (!j->s->sftp) j->s->sftp = yt_sftp_open(j->s);
            if (j->s->sftp && !j->s->dead &&
                yt_sftp_listdir(j->s->sftp, j->cwd, list, 16384) >= 0)
                ok = 1;
        }
    }
    if (done) {
        done->s = j->s; /* 所有权交给 UI，apply 里 unref */
        done->ok = ok;
        done->list = list;
        if (!PostMessageW(j->hwnd, WM_YT_SFTP_DONE, 0, (LPARAM)done)) {
            yt_session_unref(j->s);
            free(list);
            free(done);
        }
    } else {
        yt_session_unref(j->s);
        free(list);
        InterlockedExchange(&g_sftp_busy, 0);
    }
    free(j);
    return 0;
}

static void refresh_sftp(void);

static void apply_sftp_list(SftpDoneMsg *done)
{
    int cur;
    char *line, *ctx = NULL;
    InterlockedExchange(&g_sftp_busy, 0);
    if (!done) {
        if (InterlockedExchange(&g_sftp_pending, 0)) refresh_sftp();
        return;
    }
    cur = yt_app_cur_tab();
    if (!g.sftp_visible || cur < 0 || g.sess[cur] != done->s) {
        yt_session_unref(done->s);
        free(done->list);
        free(done);
        if (InterlockedExchange(&g_sftp_pending, 0)) refresh_sftp();
        return;
    }
    SendMessageW(g.sftp, LB_RESETCONTENT, 0, 0);
    SendMessageW(g.sftp, LB_ADDSTRING, 0, (LPARAM)L"D\t..");
    if (!done->ok || !done->list) {
        yt_app_set_status(L"SFTP 列表失败");
    } else {
        line = strtok_s(done->list, "\n", &ctx);
        while (line) {
            wchar_t w[512];
            MultiByteToWideChar(CP_UTF8, 0, line, -1, w, 512);
            SendMessageW(g.sftp, LB_ADDSTRING, 0, (LPARAM)w);
            line = strtok_s(NULL, "\n", &ctx);
        }
        yt_app_set_status(L"SFTP 已刷新");
        refresh_status_bar();
    }
    yt_session_unref(done->s);
    free(done->list);
    free(done);
    if (InterlockedExchange(&g_sftp_pending, 0)) refresh_sftp();
}

static void refresh_sftp(void)
{
    int cur = yt_app_cur_tab();
    char path[512];
    wchar_t wpath[520];
    SftpListJob *job;
    HANDLE thr;
    YtSession *s;
    if (cur < 0 || !g.sess[cur] || g.sess[cur]->kind != YT_KIND_SSH) return;
    s = g.sess[cur];
    if (!g.sftp_cwd[0]) strcpy(g.sftp_cwd, ".");
    GetWindowTextW(g.sftp_path, wpath, 520);
    if (wpath[0]) WideCharToMultiByte(CP_UTF8, 0, wpath, -1, path, sizeof(path), NULL, NULL);
    else { strncpy(path, g.sftp_cwd, sizeof(path) - 1); path[sizeof(path) - 1] = 0; }
    strncpy(g.sftp_cwd, path, sizeof(g.sftp_cwd) - 1);
    MultiByteToWideChar(CP_UTF8, 0, g.sftp_cwd, -1, wpath, 520);
    SetWindowTextW(g.sftp_path, wpath);
    if (InterlockedCompareExchange(&g_sftp_busy, 1, 0) != 0) {
        InterlockedExchange(&g_sftp_pending, 1);
        return;
    }
    job = (SftpListJob *)calloc(1, sizeof(*job));
    if (!job) { InterlockedExchange(&g_sftp_busy, 0); return; }
    yt_session_ref(s);
    job->s = s;
    job->hwnd = g.hwnd;
    strncpy(job->cwd, g.sftp_cwd, sizeof(job->cwd) - 1);
    SendMessageW(g.sftp, LB_RESETCONTENT, 0, 0);
    SendMessageW(g.sftp, LB_ADDSTRING, 0, (LPARAM)L"… 加载中");
    thr = CreateThread(NULL, 0, sftp_list_thr, job, 0, NULL);
    if (!thr) {
        yt_session_unref(s);
        free(job);
        InterlockedExchange(&g_sftp_busy, 0);
        yt_app_set_status(L"SFTP 刷新失败");
        return;
    }
    CloseHandle(thr);
}

static void sftp_enter_selected(void)
{
    int cur = yt_app_cur_tab(), sel;
    wchar_t item[512];
    char line[512], *name, newpath[512];
    if (cur < 0 || !g.sess[cur] || !g.sess[cur]->sftp) return;
    sel = (int)SendMessageW(g.sftp, LB_GETCURSEL, 0, 0);
    if (sel < 0) return;
    SendMessageW(g.sftp, LB_GETTEXT, sel, (LPARAM)item);
    WideCharToMultiByte(CP_UTF8, 0, item, -1, line, sizeof(line), NULL, NULL);
    if (line[0] != 'D') return;
    name = strchr(line, '\t'); if (!name) return; name++;
    if (strcmp(name, "..") == 0) {
        char *slash = strrchr(g.sftp_cwd, '/');
        if (slash && slash != g.sftp_cwd) *slash = 0;
        else strcpy(g.sftp_cwd, ".");
    } else {
        if (strcmp(g.sftp_cwd, ".") == 0) snprintf(newpath, sizeof(newpath), "%s", name);
        else snprintf(newpath, sizeof(newpath), "%s/%s", g.sftp_cwd, name);
        strncpy(g.sftp_cwd, newpath, sizeof(g.sftp_cwd) - 1);
    }
    {
        wchar_t w[520];
        MultiByteToWideChar(CP_UTF8, 0, g.sftp_cwd, -1, w, 520);
        SetWindowTextW(g.sftp_path, w);
    }
    refresh_sftp();
}

int yt_app_add_tab(YtSession *s)
{
    TCITEMW ti = {0};
    HWND term;
    int idx;
    if (g.ntabs >= YT_MAX_TABS) return -1;
    idx = g.ntabs;
    term = term_create(g.hwnd, ID_TERM_BASE + idx, g.hi);
    ui_bridge_set_id(term, "term.view");
    g.terms[idx] = term;
    g.sess[idx] = s;
    s->hwnd_main = g.hwnd;
    s->hwnd_term = term;
    s->tab_index = idx;
    term_attach(term, s);
    ti.mask = TCIF_TEXT;
    ti.pszText = s->title[0] ? s->title : L"新建";
    SendMessageW(g.tab, TCM_INSERTITEMW, idx, (LPARAM)&ti);
    g.ntabs++;
    SendMessageW(g.tab, TCM_SETCURSEL, idx, 0);
    if (s->kind == YT_KIND_SSH) g.sftp_visible = TRUE;
    yt_app_layout();
    return idx;
}

void yt_app_close_tab(int idx)
{
    int i;
    if (idx < 0 || idx >= g.ntabs) return;
    ui_bridge_log("tab close begin idx=%d left=%d", idx, g.ntabs);
    g.sess[idx]->dead = 1;
    yt_session_free(g.sess[idx]); /* 内含 unref；Agent 仍持有时延后真正 free */
    g.sess[idx] = NULL;
    DestroyWindow(g.terms[idx]);
    SendMessageW(g.tab, TCM_DELETEITEM, idx, 0);
    for (i = idx; i < g.ntabs - 1; i++) {
        g.terms[i] = g.terms[i + 1];
        g.sess[i] = g.sess[i + 1];
        g.sess[i]->tab_index = i;
    }
    g.ntabs--;
    if (g.ntabs) SendMessageW(g.tab, TCM_SETCURSEL, idx < g.ntabs ? idx : g.ntabs - 1, 0);
    else g.sftp_visible = FALSE;
    yt_app_layout();
    ui_bridge_log("tab close done left=%d", g.ntabs);
}

void yt_app_start_ok(YtSession *s, BOOL ok)
{
    if (!ok) {
        wchar_t w[300];
        int idx = s->tab_index;
        MultiByteToWideChar(CP_UTF8, 0, s->err[0] ? s->err : "connect failed", -1, w, 300);
        yt_app_set_status(w);
        if (idx >= 0 && idx < g.ntabs && g.sess[idx] == s) yt_app_close_tab(idx);
        else yt_session_free(s);
        return;
    }
    if (!s->hwnd_term) yt_app_add_tab(s);
    yt_app_set_status(L"已连接");
}

static void do_quick_connect_dlg(HWND hwnd)
{
    YtSession tmp;
    yt_session_init(&tmp);
    tmp.kind = YT_KIND_SSH;
    wcscpy(tmp.name, L"快速连接");
    GetWindowTextW(g.host, tmp.host, YT_HOST_MAX);
    { wchar_t b[16]; GetWindowTextW(g.port, b, 16); tmp.port = _wtoi(b); if (!tmp.port) tmp.port = 22; }
    GetWindowTextW(g.user, tmp.user, 64);
    GetWindowTextW(g.pass, tmp.pass, 128);
    GetWindowTextW(g.key, tmp.keyfile, YT_PATH_MAX);
    GetWindowTextW(g.jump, tmp.jump_host, YT_HOST_MAX);
    if (!yt_dlg_session_prop(hwnd, g.hi, &tmp)) return;
    SetWindowTextW(g.host, tmp.host);
    { wchar_t b[16]; swprintf(b, 16, L"%d", tmp.port); SetWindowTextW(g.port, b); }
    SetWindowTextW(g.user, tmp.user);
    SetWindowTextW(g.pass, tmp.pass);
    SetWindowTextW(g.key, tmp.keyfile);
    SetWindowTextW(g.jump, tmp.jump_host);
    SetWindowTextW(g.com, tmp.com);
    { wchar_t b[16]; swprintf(b, 16, L"%d", tmp.baud); SetWindowTextW(g.baud, b); }
    SendMessageW(g.shell, CB_SETCURSEL, (_wcsicmp(tmp.shell, L"cmd") == 0) ? 1 : 0, 0);
    {
        wchar_t fw[512];
        MultiByteToWideChar(CP_UTF8, 0, tmp.forwards, -1, fw, 512);
        SetWindowTextW(g.fwd, fw);
    }
    SendMessageW(g.cmb, CB_SETCURSEL, (WPARAM)tmp.kind, 0);
    yt_app_layout();
    yt_app_do_connect();
}

static void send_quick_text(const wchar_t *w)
{
    int cur = yt_app_cur_tab();
    char u8[2048]; int n;
    if (cur < 0 || !w || !w[0]) return;
    yt_hist_add(w);
    if (yt_macro_recording()) yt_macro_push(w);
    n = WideCharToMultiByte(CP_UTF8, 0, w, -1, u8, sizeof(u8), NULL, NULL);
    if (n > 1) {
        yt_session_write(g.sess[cur], u8, n - 1);
        yt_session_write(g.sess[cur], "\r", 1);
        if (g.sess[cur]->sync_input) {
            yt_app_sync_write(g.sess[cur], u8, n - 1);
            yt_app_sync_write(g.sess[cur], "\r", 1);
        }
    }
}

static void on_state(char *out, int cap, void *user)
{
    int cur = yt_app_cur_tab();
    YtSession *s = (cur >= 0 && cur < g.ntabs) ? g.sess[cur] : NULL;
    char title[128] = "";
    (void)user;
    if (s) WideCharToMultiByte(CP_UTF8, 0, s->title, -1, title, sizeof(title), NULL, NULL);
    snprintf(out, (size_t)cap,
             "{\"tabs\":%d,\"cur\":%d,\"kind\":%d,\"state\":%d,\"title\":\"%s\",\"sftp\":%s,\"sync\":%s,\"version\":\"1.0.0\"}",
             g.ntabs, cur, s ? (int)s->kind : -1, s ? (int)s->state : -1, title,
             g.sftp_visible ? "true" : "false",
             (s && s->sync_input) ? "true" : "false");
}

static void create_ui(HWND hwnd)
{
    int i;
    g.font_ui = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                            0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g.font_ui_bold = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                                 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g.brush_chrome = CreateSolidBrush(COL_CHROME);
    g.brush_cap = CreateSolidBrush(COL_CAP);
    g.brush_white = CreateSolidBrush(RGB(255, 255, 255));
    g.brush_status = CreateSolidBrush(RGB(236, 236, 240));
    g.sm_visible = TRUE;
    g.bb_visible = TRUE;
    g.tb_visible = TRUE;
    g.cb_visible = TRUE;
    g.st_visible = TRUE;
    g.sftp_visible = FALSE;
    g.opacity = 255;
    g.font_px = 15;
    g.theme = 0;

    g.tb[0] = mk_tb(hwnd, ID_TB_CONNECT, L"▶ 连接");
    g.tb[1] = mk_tb(hwnd, ID_TB_QUICK, L"⚡ 快速连接");
    g.tb[2] = mk_tb(hwnd, ID_TB_LOCAL, L"💻 本地");
    g.tb[3] = mk_tb(hwnd, ID_TB_DISC, L"■ 断开");
    g.tb[4] = mk_tb(hwnd, ID_TB_RECONN, L"↺ 重连");
    g.tb[5] = mk_tb(hwnd, ID_TB_SYNC, L"⇄ 同步");
    g.tb[6] = mk_tb(hwnd, ID_TB_PROP, L"☰ 属性");
    g.tb[7] = mk_tb(hwnd, ID_TB_SFTP, L"📁 SFTP");
    g.tb[8] = mk_tb(hwnd, ID_TB_FINDGO, L"🔍");

    g.cmb = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                          0, 0, 78, 200, hwnd, (HMENU)ID_CMB_KIND, g.hi, 0);
    SendMessageW(g.cmb, CB_ADDSTRING, 0, (LPARAM)L"Local");
    SendMessageW(g.cmb, CB_ADDSTRING, 0, (LPARAM)L"SSH");
    SendMessageW(g.cmb, CB_ADDSTRING, 0, (LPARAM)L"Serial");
    SendMessageW(g.cmb, CB_ADDSTRING, 0, (LPARAM)L"Telnet");
    SendMessageW(g.cmb, CB_SETCURSEL, 1, 0);
    g.host = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                           0, 0, 100, 24, hwnd, (HMENU)ID_HOST, g.hi, 0);
    g.port = CreateWindowW(L"EDIT", L"22", WS_CHILD | WS_VISIBLE | WS_BORDER, 0, 0, 50, 24, hwnd, (HMENU)ID_PORT, g.hi, 0);
    g.user = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 0, 0, 70, 24, hwnd, (HMENU)ID_USER, g.hi, 0);
    g.pass = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_PASSWORD, 0, 0, 70, 24, hwnd, (HMENU)ID_PASS, g.hi, 0);
    g.cb_go = mk_tb(hwnd, ID_CB_GO, L"连接");
    g.key = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd, (HMENU)ID_KEY, g.hi, 0);
    g.jump = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd, (HMENU)ID_JUMP, g.hi, 0);
    g.fwd = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd, (HMENU)ID_FWD, g.hi, 0);
    g.com = CreateWindowW(L"EDIT", L"COM3", WS_CHILD | WS_BORDER, 0, 0, 90, 24, hwnd, (HMENU)ID_COM, g.hi, 0);
    g.baud = CreateWindowW(L"EDIT", L"115200", WS_CHILD | WS_BORDER, 0, 0, 100, 24, hwnd, (HMENU)ID_BAUD, g.hi, 0);
    g.shell = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST, 0, 0, 140, 120, hwnd, (HMENU)ID_SHELL, g.hi, 0);
    SendMessageW(g.shell, CB_ADDSTRING, 0, (LPARAM)L"powershell");
    SendMessageW(g.shell, CB_ADDSTRING, 0, (LPARAM)L"cmd");
    SendMessageW(g.shell, CB_SETCURSEL, 0, 0);
    g.find = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 100, 24, hwnd, (HMENU)ID_FIND, g.hi, 0);

    g.sm_cap = CreateWindowW(L"STATIC", L"  会话管理器", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                             0, 0, 100, CAP_H, hwnd, (HMENU)ID_SM_CAP, g.hi, 0);
    g.tree = CreateWindowW(WC_TREEVIEWW, L"",
                           WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT,
                           0, 0, 200, 100, hwnd, (HMENU)ID_TREE, g.hi, 0);
    g.tab = CreateWindowW(WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 100, 28, hwnd, (HMENU)ID_TAB, g.hi, 0);
    g.sftp_cap = CreateWindowW(L"STATIC", L"  SFTP", WS_CHILD | SS_LEFT | SS_CENTERIMAGE, 0, 0, 100, CAP_H, hwnd, (HMENU)ID_SFTP_CAP, g.hi, 0);
    g.sftp = CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 0, 0, 200, 100, hwnd, (HMENU)ID_SFTP, g.hi, 0);
    g.sftp_path = CreateWindowW(L"EDIT", L".", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 200, 22, hwnd, (HMENU)ID_SFTP_PATH, g.hi, 0);
    g.btn_sref = mk_tb(hwnd, ID_BTN_SFTP_REF, L"刷新");
    g.btn_sup = mk_tb(hwnd, ID_BTN_SFTP_UP, L"上传");
    g.btn_sdn = mk_tb(hwnd, ID_BTN_SFTP_DN, L"下载");
    ShowWindow(g.btn_sref, SW_HIDE); ShowWindow(g.btn_sup, SW_HIDE); ShowWindow(g.btn_sdn, SW_HIDE);

    for (i = 0; i < BB_N; i++)
        g.bb[i] = mk_tb(hwnd, ID_BB0 + i, k_bb_cmd[i]);
    g.quick = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 400, 24, hwnd, (HMENU)ID_QUICK, g.hi, 0);
    g.btn_quick = mk_tb(hwnd, ID_BTN_QUICK, L"发送");
    g.status = CreateWindowW(L"STATIC", L"  就绪", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                             0, 0, 100, ST_H, hwnd, (HMENU)ID_STATUS, g.hi, 0);
    strcpy(g.sftp_cwd, ".");

    {
        HWND all[] = {
            g.cmb, g.host, g.port, g.user, g.pass, g.find, g.tree, g.tab, g.sftp, g.sftp_path,
            g.quick, g.status, g.sm_cap, g.sftp_cap, g.key, g.jump, g.fwd, g.com, g.baud, g.shell
        };
        for (i = 0; i < (int)(sizeof(all)/sizeof(all[0])); i++)
            SendMessageW(all[i], WM_SETFONT, (WPARAM)g.font_ui, TRUE);
        SendMessageW(g.sm_cap, WM_SETFONT, (WPARAM)g.font_ui_bold, TRUE);
        SendMessageW(g.sftp_cap, WM_SETFONT, (WPARAM)g.font_ui_bold, TRUE);
    }
    SendMessageW(g.find, EM_SETCUEBANNER, FALSE, (LPARAM)L"在终端中查找...");
    SendMessageW(g.host, EM_SETCUEBANNER, FALSE, (LPARAM)L"主机名或 IP");
    SendMessageW(g.quick, EM_SETCUEBANNER, FALSE, (LPARAM)L"按钮栏：输入命令后发送");

    ui_bridge_set_id(g.cmb, "cmb.kind");
    ui_bridge_set_id(g.host, "edit.ssh.host");
    ui_bridge_set_id(g.port, "edit.ssh.port");
    ui_bridge_set_id(g.user, "edit.ssh.user");
    ui_bridge_set_id(g.pass, "edit.ssh.pass");
    ui_bridge_set_id(g.cb_go, "btn.connect");
    ui_bridge_set_id(g.tb[0], "btn.toolbar.connect");
    ui_bridge_set_id(g.tb[2], "btn.local");
    ui_bridge_set_id(g.tb[3], "btn.close");
    ui_bridge_set_id(g.tb[5], "btn.sync");
    ui_bridge_set_id(g.tree, "tree.sessions");
    ui_bridge_set_id(g.tab, "tab.main");
    ui_bridge_set_id(g.sftp, "list.sftp");
    ui_bridge_set_id(g.btn_sref, "btn.sftp.refresh");
    ui_bridge_set_id(g.btn_sup, "btn.sftp.upload");
    ui_bridge_set_id(g.btn_sdn, "btn.sftp.download");
    ui_bridge_set_id(g.status, "label.status");
    ui_bridge_set_id(g.find, "edit.find");
    ui_bridge_set_id(g.sftp_path, "edit.sftp.path");
    ui_bridge_set_id(g.quick, "edit.quick");
    ui_bridge_set_id(g.btn_quick, "btn.quick");
    ui_bridge_set_id(g.sm_cap, "label.session.manager");
    ui_bridge_set_id(g.com, "edit.serial.com");
    ui_bridge_set_id(g.baud, "edit.serial.baud");
    ui_bridge_set_id(g.shell, "cmb.shell");
    SendMessageW(g.com, EM_SETCUEBANNER, FALSE, (LPARAM)L"COMx");
    SendMessageW(g.baud, EM_SETCUEBANNER, FALSE, (LPARAM)L"波特率");
}

static HMENU build_menu(void)
{
    HMENU bar = CreateMenu();
    HMENU f = CreatePopupMenu(), e = CreatePopupMenu(), v = CreatePopupMenu();
    HMENU s = CreatePopupMenu(), x = CreatePopupMenu(), m = CreatePopupMenu();
    HMENU t = CreatePopupMenu(), w = CreatePopupMenu(), h = CreatePopupMenu();
    HMENU fn = CreatePopupMenu(), fv = CreatePopupMenu(), fp = CreatePopupMenu();
    HMENU fo = CreatePopupMenu(), fz = CreatePopupMenu();

    /* 文件 — 对齐 SecureCRT/Xshell */
    AppendMenuW(fn, MF_STRING, IDM_NEW_LOCAL, L"PowerShell");
    AppendMenuW(fn, MF_STRING, IDM_NEW_CMD, L"CMD");
    AppendMenuW(fn, MF_STRING, IDM_NEW_ADMIN, L"管理员 PowerShell");
    AppendMenuW(f, MF_POPUP, (UINT_PTR)fn, L"新建本地");
    AppendMenuW(f, MF_STRING, IDM_QUICK_CONN, L"快速连接...\tCtrl+Shift+N");
    AppendMenuW(f, MF_STRING, IDM_CONNECT, L"连接\tEnter");
    AppendMenuW(f, MF_STRING, IDM_DISCONNECT, L"断开\tCtrl+W");
    AppendMenuW(f, MF_STRING, IDM_RECONNECT, L"重新连接\tCtrl+R");
    AppendMenuW(f, MF_STRING, IDM_CLONE_TAB, L"克隆会话");
    AppendMenuW(f, MF_SEPARATOR, 0, NULL);
    AppendMenuW(f, MF_STRING, IDM_CLOSE_TAB, L"关闭标签");
    AppendMenuW(f, MF_STRING, IDM_TAB_CLOSE_OTHER, L"关闭其他标签");
    AppendMenuW(f, MF_STRING, IDM_CLOSE_ALL, L"关闭全部标签");
    AppendMenuW(f, MF_SEPARATOR, 0, NULL);
    AppendMenuW(f, MF_STRING, IDM_SAVE, L"保存会话到管理器");
    AppendMenuW(f, MF_STRING, IDM_ADD_TREE, L"当前会话加入管理器");
    AppendMenuW(f, MF_STRING, IDM_LOAD, L"刷新会话管理器");
    AppendMenuW(f, MF_STRING, IDM_EXPORT_SESS, L"导出会话配置...");
    AppendMenuW(f, MF_STRING, IDM_IMPORT_SESS, L"导入会话配置...");
    AppendMenuW(f, MF_SEPARATOR, 0, NULL);
    AppendMenuW(f, MF_STRING, IDM_LAYOUT_SAVE, L"保存窗口配置");
    AppendMenuW(f, MF_STRING, IDM_LAYOUT_LOAD, L"恢复窗口配置");
    AppendMenuW(f, MF_STRING, IDM_OPEN_CONFIG, L"打开配置目录");
    AppendMenuW(f, MF_STRING, IDM_OPEN_LOGDIR, L"打开日志目录");
    AppendMenuW(f, MF_SEPARATOR, 0, NULL);
    AppendMenuW(f, MF_STRING, IDM_EXIT, L"退出\tAlt+F4");

    /* 编辑 */
    AppendMenuW(e, MF_STRING, IDM_EDIT_COPY, L"复制\tCtrl+Shift+C");
    AppendMenuW(e, MF_STRING, IDM_EDIT_PASTE, L"粘贴\tCtrl+V");
    AppendMenuW(e, MF_STRING, IDM_EDIT_ALL, L"全选\tCtrl+A");
    AppendMenuW(e, MF_STRING, IDM_EDIT_CLR, L"清屏");
    AppendMenuW(e, MF_SEPARATOR, 0, NULL);
    AppendMenuW(e, MF_STRING, IDM_FIND, L"查找...\tCtrl+F");
    AppendMenuW(e, MF_STRING, IDM_FIND_NEXT, L"查找下一个\tF3");
    AppendMenuW(e, MF_SEPARATOR, 0, NULL);
    AppendMenuW(fp, MF_STRING, IDM_PASTE_0, L"无延迟");
    AppendMenuW(fp, MF_STRING, IDM_PASTE_10, L"10 ms/字符");
    AppendMenuW(fp, MF_STRING, IDM_PASTE_50, L"50 ms/字符");
    AppendMenuW(fp, MF_STRING, IDM_PASTE_100, L"100 ms/字符");
    AppendMenuW(e, MF_POPUP, (UINT_PTR)fp, L"粘贴延迟");
    AppendMenuW(e, MF_STRING, IDM_SYNC_TOGGLE, L"同步输入 开/关");
    AppendMenuW(e, MF_STRING, IDM_COMPLETE, L"命令补全\tTab");

    /* 查看 */
    AppendMenuW(v, MF_STRING, IDM_VIEW_SM, L"会话管理器");
    AppendMenuW(v, MF_STRING, IDM_VIEW_TB, L"工具栏");
    AppendMenuW(v, MF_STRING, IDM_VIEW_CB, L"连接栏");
    AppendMenuW(v, MF_STRING, IDM_VIEW_BB, L"按钮栏");
    AppendMenuW(v, MF_STRING, IDM_VIEW_ST, L"状态栏");
    AppendMenuW(v, MF_STRING, IDM_VIEW_SFTP, L"SFTP 面板");
    AppendMenuW(v, MF_SEPARATOR, 0, NULL);
    AppendMenuW(fv, MF_STRING, IDM_SPLIT_OFF, L"关闭分屏");
    AppendMenuW(fv, MF_STRING, IDM_SPLIT_H, L"水平分屏");
    AppendMenuW(fv, MF_STRING, IDM_SPLIT_V, L"垂直分屏");
    AppendMenuW(fv, MF_STRING, IDM_SPLIT, L"循环切换分屏");
    AppendMenuW(v, MF_POPUP, (UINT_PTR)fv, L"分屏");
    AppendMenuW(v, MF_SEPARATOR, 0, NULL);
    AppendMenuW(v, MF_STRING, IDM_FULLSCREEN, L"全屏\tF11");
    AppendMenuW(v, MF_STRING, IDM_ONTOP, L"窗口置顶");
    AppendMenuW(fo, MF_STRING, IDM_OPACITY100, L"不透明 100%");
    AppendMenuW(fo, MF_STRING, IDM_OPACITY90, L"透明度 90%");
    AppendMenuW(fo, MF_STRING, IDM_OPACITY80, L"透明度 80%");
    AppendMenuW(v, MF_POPUP, (UINT_PTR)fo, L"透明度");
    AppendMenuW(fz, MF_STRING, IDM_ZOOM_IN, L"放大字体\tCtrl+=");
    AppendMenuW(fz, MF_STRING, IDM_ZOOM_OUT, L"缩小字体\tCtrl+-");
    AppendMenuW(fz, MF_STRING, IDM_ZOOM_RESET, L"重置字体");
    AppendMenuW(v, MF_POPUP, (UINT_PTR)fz, L"缩放");
    AppendMenuW(v, MF_STRING, IDM_THEME, L"浅色铬");
    AppendMenuW(v, MF_STRING, IDM_THEME_DARK, L"深色铬");

    /* 会话 */
    AppendMenuW(s, MF_STRING, IDM_PROP, L"会话选项...");
    AppendMenuW(s, MF_STRING, IDM_LOG, L"开关会话日志");
    AppendMenuW(s, MF_STRING, IDM_LOCK, L"锁定/解锁输入");
    AppendMenuW(s, MF_STRING, IDM_TAB_COLOR, L"标签着色");
    AppendMenuW(s, MF_STRING, IDM_SEND_STR, L"发送字符串...");
    AppendMenuW(s, MF_SEPARATOR, 0, NULL);
    AppendMenuW(s, MF_STRING, IDM_RECONNECT, L"重新连接");
    AppendMenuW(s, MF_STRING, IDM_DISCONNECT, L"断开");

    /* 传输 */
    AppendMenuW(x, MF_STRING, IDM_ZRX, L"ZMODEM 接收");
    AppendMenuW(x, MF_STRING, IDM_ZTX, L"ZMODEM 发送...");
    AppendMenuW(x, MF_SEPARATOR, 0, NULL);
    AppendMenuW(x, MF_STRING, IDM_SFTP_REF, L"SFTP 刷新");
    AppendMenuW(x, MF_STRING, IDM_SFTP_UP, L"SFTP 上传...");
    AppendMenuW(x, MF_STRING, IDM_SFTP_DN, L"SFTP 下载");
    AppendMenuW(x, MF_STRING, IDM_LOCAL_FILES, L"打开本地面板");
    AppendMenuW(x, MF_STRING, IDM_DUMP, L"导出屏幕文本");

    /* 脚本 */
    AppendMenuW(m, MF_STRING, IDM_MACRO_REC, L"开始录制宏");
    AppendMenuW(m, MF_STRING, IDM_MACRO_STOP, L"停止录制");
    AppendMenuW(m, MF_STRING, IDM_MACRO_RUN, L"执行宏");
    AppendMenuW(m, MF_STRING, IDM_MACRO_SAVE, L"保存宏");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_SCRIPT, L"运行脚本...");

    /* 工具 */
    AppendMenuW(t, MF_STRING, IDM_COM_ENUM, L"枚举串口");
    AppendMenuW(t, MF_STRING, IDM_X11, L"启动 X11");
    AppendMenuW(t, MF_STRING, IDM_UNIX, L"Unix 工具目录");
    AppendMenuW(t, MF_STRING, IDM_OPEN_KEYS, L"密钥目录");
    AppendMenuW(t, MF_STRING, IDM_OPEN_RUNTIME, L"Runtime 目录");
    AppendMenuW(t, MF_STRING, IDM_CRED_HINT, L"凭证存储说明");
    AppendMenuW(t, MF_STRING, IDM_STATS, L"状态栏资源监控");

    /* 窗口 */
    AppendMenuW(w, MF_STRING, IDM_TAB_NEXT, L"下一标签\tCtrl+Tab");
    AppendMenuW(w, MF_STRING, IDM_TAB_PREV, L"上一标签\tCtrl+Shift+Tab");
    AppendMenuW(w, MF_STRING, IDM_NEW_WINDOW, L"新建窗口");

    /* 帮助 */
    AppendMenuW(h, MF_STRING, IDM_HOTKEYS, L"快捷键一览");
    AppendMenuW(h, MF_STRING, IDM_ABOUT, L"关于 YTerminal");

    AppendMenuW(bar, MF_POPUP, (UINT_PTR)f, L"文件");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)e, L"编辑");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)v, L"查看");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)s, L"会话");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)x, L"传输");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)m, L"脚本");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)t, L"工具");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)w, L"窗口");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)h, L"帮助");
    return bar;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ui_bridge_handle_message(hwnd, msg, wp, lp)) return 0;
    switch (msg) {
    case WM_CREATE: {
        YtWindowCfg wc;
        g.hwnd = hwnd; /* G-01: 先于 create_ui/终端创建 */
        create_ui(hwnd);
        yt_app_set_sync_hook(sync_hook);
        {
            YtAgentHooks ah;
            ah.ntabs = yt_app_tab_count;
            ah.tab_at = yt_app_tab_at;
            yt_agent_set_hooks(&ah);
        }
        ui_bridge_start(hwnd, 8086);
        ui_bridge_set_state_provider(on_state, NULL);
        yt_macro_load();
        yt_app_sessions_reload(); /* 每次启动必读 sessions.json */
        if (yt_window_load(&wc)) {
            g.sm_visible = wc.sm_visible;
            g.bb_visible = wc.bb_visible;
            g.sftp_visible = wc.sftp_visible;
            g.split_mode = wc.split;
            g.theme = wc.theme;
            g.font_px = wc.font_px > 0 ? wc.font_px : 15;
            g.stats_on = wc.stats;
            yt_set_paste_delay(wc.paste_delay_ms);
            SendMessageW(g.cmb, CB_SETCURSEL, (WPARAM)wc.kind, 0);
            if (wc.opacity > 0 && wc.opacity < 255) apply_opacity((wc.opacity * 100) / 255);
        }
        {
            YtSession *s = (YtSession *)calloc(1, sizeof(YtSession));
            yt_session_init(s);
            s->kind = YT_KIND_LOCAL;
            swprintf(s->title, YT_TITLE_MAX, L"Local:%ls", s->shell);
            yt_app_add_tab(s);
            if (!yt_local_start(s)) yt_app_start_ok(s, FALSE);
            else yt_app_set_status(L"已连接");
            if (g.font_px != 15) apply_font_all(g.font_px);
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == IDT_STATS) refresh_status_bar();
        return 0;
    case WM_SIZE: yt_app_layout(); return 0;
    case WM_SHOWWINDOW:
        if (wp) {
            PostMessageW(hwnd, WM_SIZE, 0, 0);
            /* 启动/再显示时从磁盘刷新会话树 */
            yt_app_sessions_reload();
        }
        break;
    case WM_PAINT: paint_chrome(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        HWND ctrl = (HWND)lp;
        SetTextColor(hdc, RGB(40, 40, 45));
        if (ctrl == g.sm_cap || ctrl == g.sftp_cap) {
            SetBkColor(hdc, COL_CAP);
            return (LRESULT)g.brush_cap;
        }
        if (ctrl == g.status) {
            SetBkColor(hdc, RGB(236, 236, 240));
            return (LRESULT)g.brush_status;
        }
        SetBkColor(hdc, COL_CHROME);
        return (LRESULT)g.brush_chrome;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, RGB(20, 20, 20));
        SetBkColor(hdc, RGB(255, 255, 255));
        return (LRESULT)g.brush_white;
    }
    case WM_YT_TITLE: refresh_status_bar(); return 0;
    case WM_NOTIFY:
        if (((LPNMHDR)lp)->idFrom == ID_TAB && ((LPNMHDR)lp)->code == TCN_SELCHANGE) {
            int cur = yt_app_cur_tab();
            /* 切标签绝不在 UI 线程同步做 SFTP（会与 SSH reader 死锁） */
            if (cur >= 0 && g.sess[cur] && g.sess[cur]->kind == YT_KIND_SSH)
                g.sftp_visible = TRUE;
            else if (cur >= 0 && g.sess[cur] && g.sess[cur]->kind != YT_KIND_SSH)
                g.sftp_visible = FALSE;
            yt_app_layout();
            if (g.sftp_visible)
                PostMessageW(hwnd, WM_YT_SFTP_REFRESH, 0, 0);
        }
        if (((LPNMHDR)lp)->idFrom == ID_TREE && ((LPNMHDR)lp)->code == NM_DBLCLK)
            yt_app_tree_connect_index(yt_app_tree_sel_index());
        if (((LPNMHDR)lp)->idFrom == ID_TREE && ((LPNMHDR)lp)->code == NM_RCLICK) {
            POINT pt; HMENU m = CreatePopupMenu();
            GetCursorPos(&pt);
            AppendMenuW(m, MF_STRING, IDM_TREE_CONN, L"连接");
            AppendMenuW(m, MF_STRING, IDM_TREE_EDIT, L"会话选项...");
            AppendMenuW(m, MF_STRING, IDM_TREE_CLONE, L"克隆");
            AppendMenuW(m, MF_STRING, IDM_TREE_DEL, L"删除");
            TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(m);
        }
        if (((LPNMHDR)lp)->idFrom == ID_TAB && ((LPNMHDR)lp)->code == NM_RCLICK) {
            POINT pt; HMENU m = CreatePopupMenu();
            GetCursorPos(&pt);
            AppendMenuW(m, MF_STRING, ID_TB_DISC, L"断开/关闭");
            AppendMenuW(m, MF_STRING, IDM_TAB_CLOSE_OTHER, L"关闭其他");
            AppendMenuW(m, MF_STRING, IDM_TAB_COLOR, L"着色");
            AppendMenuW(m, MF_STRING, IDM_RECONNECT, L"重连");
            TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(m);
        }
        if (((LPNMHDR)lp)->idFrom == ID_SFTP && ((LPNMHDR)lp)->code == NM_DBLCLK)
            sftp_enter_selected();
        return 0;
    case WM_COMMAND:
        if (HIWORD(wp) == CBN_SELCHANGE && LOWORD(wp) == ID_CMB_KIND) {
            int k = yt_app_kind_sel();
            wchar_t b[16];
            if (k == 1) { swprintf(b, 16, L"%d", 22); SetWindowTextW(g.port, b); }
            else if (k == 2) { SetWindowTextW(g.port, L""); } /* S-20：串口清空残留端口 */
            else if (k == 3) { swprintf(b, 16, L"%d", 23); SetWindowTextW(g.port, b); }
            yt_app_layout();
            return 0;
        }
        if (LOWORD(wp) >= ID_BB0 && LOWORD(wp) < ID_BB0 + BB_N) {
            send_quick_text(k_bb_cmd[LOWORD(wp) - ID_BB0]);
            return 0;
        }
        switch (LOWORD(wp)) {
        case ID_TB_CONNECT:
        case IDM_TREE_CONN:
            if (yt_app_tree_sel_index() >= 0) yt_app_tree_connect_index(yt_app_tree_sel_index());
            else yt_app_do_connect();
            break;
        case ID_CB_GO: yt_app_do_connect(); break;
        case ID_TB_QUICK:
        case IDM_QUICK_CONN: do_quick_connect_dlg(hwnd); break;
        case ID_TB_LOCAL: yt_app_start_local(NULL, FALSE); break;
        case IDM_NEW_LOCAL: yt_app_start_local(L"powershell", FALSE); break;
        case IDM_NEW_CMD: yt_app_start_local(L"cmd", FALSE); break;
        case IDM_NEW_ADMIN: yt_app_start_local(L"powershell", TRUE); break;
        case IDM_CONNECT: yt_app_do_connect(); break;
        case ID_TB_DISC:
        case IDM_DISCONNECT:
        case IDM_CLOSE_TAB: yt_app_close_tab(yt_app_cur_tab()); break;
        case IDM_CLONE_TAB: yt_app_clone_cur(); break;
        case IDM_CLOSE_ALL: while (g.ntabs > 0) yt_app_close_tab(0); break;
        case ID_TB_RECONN:
        case IDM_RECONNECT:
            yt_app_reconnect_cur();
            break;
        case ID_TB_SYNC: {
            int cur = yt_app_cur_tab(), i;
            if (cur >= 0) {
                BOOL v = !g.sess[cur]->sync_input;
                for (i = 0; i < g.ntabs; i++) g.sess[i]->sync_input = v;
                refresh_status_bar();
            }
            break;
        }
        case ID_TB_PROP:
        case IDM_PROP: {
            int cur = yt_app_cur_tab();
            if (cur >= 0 && yt_dlg_session_prop(hwnd, g.hi, g.sess[cur])) {
                TCITEMW ti = {0};
                ti.mask = TCIF_TEXT;
                ti.pszText = g.sess[cur]->name[0] ? g.sess[cur]->name : g.sess[cur]->title;
                SendMessageW(g.tab, TCM_SETITEMW, cur, (LPARAM)&ti);
                refresh_status_bar();
            }
            break;
        }
        case ID_TB_SFTP:
        case IDM_VIEW_SFTP:
            g.sftp_visible = !g.sftp_visible;
            if (g.sftp_visible) refresh_sftp();
            yt_app_layout();
            break;
        case ID_TB_FINDGO:
        case IDM_FIND: {
            int cur = yt_app_cur_tab(); wchar_t q[128];
            GetWindowTextW(g.find, q, 128);
            wcsncpy(g.last_find, q, 127);
            if (cur >= 0) term_find(g.terms[cur], q);
            break;
        }
        case ID_BTN_QUICK: {
            wchar_t w[1024];
            GetWindowTextW(g.quick, w, 1024);
            send_quick_text(w);
            break;
        }
        case ID_SFTP:
            if (HIWORD(wp) == LBN_DBLCLK) sftp_enter_selected();
            break;
        case ID_BTN_SFTP_REF: refresh_sftp(); break;
        case IDM_TREE_EDIT: {
            int i = yt_app_tree_sel_index();
            if (i >= 0 && yt_dlg_session_prop(hwnd, g.hi, &g.profiles[i])) {
                yt_app_sessions_persist();
                yt_app_tree_reload();
            }
            break;
        }
        case IDM_TREE_CLONE: {
            int i = yt_app_tree_sel_index();
            if (i >= 0 && g.tree_n < YT_MAX_SESSIONS) {
                yt_app_profile_from_session(&g.profiles[g.tree_n], &g.profiles[i]);
                wcsncat(g.profiles[g.tree_n].name, L" (copy)",
                        YT_TITLE_MAX - wcslen(g.profiles[g.tree_n].name) - 1);
                g.tree_n++;
                yt_app_sessions_persist();
                yt_app_tree_reload();
            }
            break;
        }
        case IDM_TREE_DEL:
            yt_app_tree_delete_index(yt_app_tree_sel_index());
            break;
        case ID_BTN_SFTP_UP: {
            int cur = yt_app_cur_tab();
            OPENFILENAMEW ofn = {0}; wchar_t file[YT_PATH_MAX] = {0};
            char remote[520];
            if (cur < 0 || !g.sess[cur]->sftp) break;
            ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFile = file; ofn.nMaxFile = YT_PATH_MAX;
            ofn.Flags = OFN_FILEMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                char *base; char path8[YT_PATH_MAX];
                WideCharToMultiByte(CP_UTF8, 0, file, -1, path8, sizeof(path8), NULL, NULL);
                base = strrchr(path8, '\\'); if (!base) base = path8; else base++;
                if (strcmp(g.sftp_cwd, ".") == 0) snprintf(remote, sizeof(remote), "%s", base);
                else snprintf(remote, sizeof(remote), "%s/%s", g.sftp_cwd, base);
                if (yt_sftp_upload(g.sess[cur]->sftp, file, remote) == 0) { yt_app_set_status(L"上传成功"); refresh_sftp(); }
                else yt_app_set_status(L"上传失败");
            }
            break;
        }
        case ID_BTN_SFTP_DN: {
            int cur = yt_app_cur_tab(), sel;
            wchar_t item[512], local[YT_PATH_MAX];
            char line[512], *name;
            if (cur < 0 || !g.sess[cur]->sftp) break;
            sel = (int)SendMessageW(g.sftp, LB_GETCURSEL, 0, 0);
            if (sel < 0) break;
            SendMessageW(g.sftp, LB_GETTEXT, sel, (LPARAM)item);
            WideCharToMultiByte(CP_UTF8, 0, item, -1, line, sizeof(line), NULL, NULL);
            name = strchr(line, '\t'); if (!name) break; name++;
            {
                char remote[520];
                if (strcmp(g.sftp_cwd, ".") == 0) snprintf(remote, sizeof(remote), "%s", name);
                else snprintf(remote, sizeof(remote), "%s/%s", g.sftp_cwd, name);
                swprintf(local, YT_PATH_MAX, L"%ls\\%hs", yt_paths()->logs, name);
                if (yt_sftp_download(g.sess[cur]->sftp, remote, local) == 0) yt_app_set_status(L"下载成功");
                else yt_app_set_status(L"下载失败");
            }
            break;
        }
        case IDM_SAVE:
            yt_app_sessions_persist();
            yt_app_set_status(L"已保存到 config\\sessions.json");
            break;
        case IDM_LOAD:
            yt_app_sessions_reload();
            yt_app_set_status(L"已从 sessions.json 重新加载");
            break;
        case IDM_ADD_TREE: {
            int cur = yt_app_cur_tab();
            if (cur >= 0 && g.tree_n < YT_MAX_SESSIONS) {
                yt_app_profile_from_session(&g.profiles[g.tree_n], g.sess[cur]);
                g.tree_n++;
                yt_app_sessions_persist();
                yt_app_tree_reload();
                yt_app_set_status(L"已加入并写入 sessions.json");
            }
            break;
        }
        case IDM_LOG: {
            int cur = yt_app_cur_tab();
            wchar_t path[YT_PATH_MAX]; char up[YT_PATH_MAX];
            if (cur < 0) break;
            if (g.sess[cur]->log_fp) {
                fclose(g.sess[cur]->log_fp); g.sess[cur]->log_fp = NULL; g.sess[cur]->log_on = FALSE;
                yt_app_set_status(L"日志关"); break;
            }
            swprintf(path, YT_PATH_MAX, L"%ls\\tab%d.log", yt_paths()->logs, cur);
            WideCharToMultiByte(CP_UTF8, 0, path, -1, up, sizeof(up), NULL, NULL);
            g.sess[cur]->log_fp = fopen(up, "ab");
            g.sess[cur]->log_on = g.sess[cur]->log_fp != NULL;
            yt_app_set_status(g.sess[cur]->log_on ? L"日志开" : L"日志失败");
            break;
        }
        case IDM_SPLIT:
            g.split_mode = (g.split_mode + 1) % 3;
            yt_app_layout();
            break;
        case IDM_VIEW_SM: g.sm_visible = !g.sm_visible; yt_app_layout(); break;
        case IDM_VIEW_BB: g.bb_visible = !g.bb_visible; yt_app_layout(); break;
        case IDM_VIEW_TB: g.tb_visible = !g.tb_visible; yt_app_layout(); break;
        case IDM_VIEW_CB: g.cb_visible = !g.cb_visible; yt_app_layout(); break;
        case IDM_VIEW_ST: g.st_visible = !g.st_visible; yt_app_layout(); break;
        case IDM_SPLIT_OFF: g.split_mode = 0; yt_app_layout(); break;
        case IDM_SPLIT_H: g.split_mode = 1; yt_app_layout(); break;
        case IDM_SPLIT_V: g.split_mode = 2; yt_app_layout(); break;
        case IDM_FULLSCREEN: toggle_fullscreen(hwnd); break;
        case IDM_ONTOP:
            g.always_on_top = !g.always_on_top;
            SetWindowPos(hwnd, g.always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                         0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            yt_app_set_status(g.always_on_top ? L"已置顶" : L"取消置顶");
            break;
        case IDM_OPACITY100: apply_opacity(100); break;
        case IDM_OPACITY90: apply_opacity(90); break;
        case IDM_OPACITY80: apply_opacity(80); break;
        case IDM_ZOOM_IN: apply_font_all(g.font_px + 1); break;
        case IDM_ZOOM_OUT: apply_font_all(g.font_px - 1); break;
        case IDM_ZOOM_RESET: apply_font_all(15); break;
        case IDM_THEME_DARK:
            g.theme = 1;
            if (g.brush_chrome) DeleteObject(g.brush_chrome);
            g.brush_chrome = CreateSolidBrush(RGB(45, 45, 48));
            InvalidateRect(hwnd, NULL, TRUE);
            break;
        case IDM_EDIT_COPY: { int c = yt_app_cur_tab(); if (c >= 0) term_copy(g.terms[c]); break; }
        case IDM_EDIT_PASTE: { int c = yt_app_cur_tab(); if (c >= 0) term_paste(g.terms[c]); break; }
        case IDM_EDIT_ALL: { int c = yt_app_cur_tab(); if (c >= 0) term_select_all(g.terms[c]); break; }
        case IDM_EDIT_CLR: { int c = yt_app_cur_tab(); if (c >= 0) term_clear(g.terms[c]); break; }
        case IDM_FIND_NEXT: { int c = yt_app_cur_tab(); if (c >= 0) term_find_next(g.terms[c]); break; }
        case IDM_PASTE_0: yt_set_paste_delay(0); yt_app_set_status(L"粘贴延迟: 0"); break;
        case IDM_PASTE_10: yt_set_paste_delay(10); yt_app_set_status(L"粘贴延迟: 10ms"); break;
        case IDM_PASTE_50: yt_set_paste_delay(50); yt_app_set_status(L"粘贴延迟: 50ms"); break;
        case IDM_PASTE_100: yt_set_paste_delay(100); yt_app_set_status(L"粘贴延迟: 100ms"); break;
        case IDM_SYNC_TOGGLE: {
            int c = yt_app_cur_tab();
            if (c >= 0) {
                g.sess[c]->sync_input = !g.sess[c]->sync_input;
                yt_app_set_status(g.sess[c]->sync_input ? L"同步输入: 开" : L"同步输入: 关");
                refresh_status_bar();
            }
            break;
        }
        case IDM_LOCK: {
            int c = yt_app_cur_tab();
            if (c >= 0) {
                g.sess[c]->locked = !g.sess[c]->locked;
                yt_app_set_status(g.sess[c]->locked ? L"会话已锁定" : L"会话已解锁");
            }
            break;
        }
        case IDM_SEND_STR: {
            int c = yt_app_cur_tab();
            wchar_t w[512]; char u8[1024];
            if (c < 0) break;
            GetWindowTextW(g.quick, w, 512);
            if (!w[0]) { yt_app_set_status(L"请在按钮栏输入框填写要发送的内容"); break; }
            WideCharToMultiByte(CP_UTF8, 0, w, -1, u8, sizeof(u8), NULL, NULL);
            yt_session_write(g.sess[c], u8, (int)strlen(u8));
            yt_session_write(g.sess[c], "\r", 1);
            break;
        }
        case IDM_SFTP_REF: refresh_sftp(); break;
        case IDM_SFTP_UP: PostMessageW(hwnd, WM_COMMAND, ID_BTN_SFTP_UP, 0); break;
        case IDM_SFTP_DN: PostMessageW(hwnd, WM_COMMAND, ID_BTN_SFTP_DN, 0); break;
        case IDM_LOCAL_FILES:
            ShellExecuteW(hwnd, L"open", yt_paths()->logs, NULL, NULL, SW_SHOWNORMAL);
            break;
        case IDM_OPEN_CONFIG:
            ShellExecuteW(hwnd, L"open", yt_paths()->config, NULL, NULL, SW_SHOWNORMAL);
            break;
        case IDM_OPEN_LOGDIR:
            ShellExecuteW(hwnd, L"open", yt_paths()->logs, NULL, NULL, SW_SHOWNORMAL);
            break;
        case IDM_OPEN_KEYS:
            ShellExecuteW(hwnd, L"open", yt_paths()->keys, NULL, NULL, SW_SHOWNORMAL);
            break;
        case IDM_OPEN_RUNTIME:
            ShellExecuteW(hwnd, L"open", yt_paths()->runtime, NULL, NULL, SW_SHOWNORMAL);
            break;
        case IDM_EXPORT_SESS: {
            OPENFILENAMEW ofn = {0}; wchar_t file[YT_PATH_MAX] = L"sessions_export.json";
            ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFile = file; ofn.nMaxFile = YT_PATH_MAX;
            ofn.lpstrFilter = L"JSON\0*.json\0"; ofn.Flags = OFN_OVERWRITEPROMPT;
            if (GetSaveFileNameW(&ofn)) CopyFileW(yt_paths()->sessions, file, FALSE);
            break;
        }
        case IDM_IMPORT_SESS: {
            OPENFILENAMEW ofn = {0}; wchar_t file[YT_PATH_MAX] = {0};
            ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFile = file; ofn.nMaxFile = YT_PATH_MAX;
            ofn.lpstrFilter = L"JSON\0*.json\0"; ofn.Flags = OFN_FILEMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                YtSession *tmp;
                wchar_t bak[YT_PATH_MAX];
                int n, i;
                char path8[YT_PATH_MAX], *buf = NULL;
                FILE *fp; long sz;
                WideCharToMultiByte(CP_UTF8, 0, file, -1, path8, sizeof(path8), NULL, NULL);
                fp = fopen(path8, "rb");
                if (!fp) { yt_app_set_status(L"无法打开导入文件"); break; }
                fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
                buf = (char *)malloc((size_t)sz + 1);
                if (!buf) { fclose(fp); break; }
                fread(buf, 1, (size_t)sz, fp); buf[sz] = 0; fclose(fp);
                if (!strchr(buf, '{')) {
                    free(buf);
                    MessageBoxW(hwnd, L"不是有效的会话 JSON，已取消导入", L"导入", MB_ICONWARNING);
                    break;
                }
                free(buf);
                tmp = (YtSession *)calloc((size_t)YT_MAX_SESSIONS, sizeof(YtSession));
                if (!tmp) break;
                swprintf(bak, YT_PATH_MAX, L"%ls.bak", yt_paths()->sessions);
                CopyFileW(yt_paths()->sessions, bak, FALSE);
                if (!CopyFileW(file, yt_paths()->sessions, FALSE)) {
                    free(tmp);
                    yt_app_set_status(L"导入拷贝失败");
                    break;
                }
                n = yt_profile_load(tmp, YT_MAX_SESSIONS);
                for (i = 0; i < n; i++) DeleteCriticalSection(&tmp[i].lock);
                free(tmp);
                if (n <= 0) {
                    CopyFileW(bak, yt_paths()->sessions, FALSE);
                    MessageBoxW(hwnd, L"解析失败，已恢复原 sessions.json", L"导入", MB_ICONWARNING);
                    break;
                }
                yt_app_sessions_reload();
                yt_app_set_status(L"会话已导入（原文件已备份 .bak）");
            }
            break;
        }
        case IDM_CRED_HINT:
            MessageBoxW(hwnd,
                L"密码以 DPAPI 加密保存在 config\\creds.bin\n"
                L"连接配置：config\\sessions.json\n"
                L"窗口配置：config\\window.json",
                L"凭证与配置", MB_OK | MB_ICONINFORMATION);
            break;
        case IDM_TAB_NEXT:
            if (g.ntabs > 0)
                SendMessageW(g.tab, TCM_SETCURSEL, (yt_app_cur_tab() + 1) % g.ntabs, 0), yt_app_layout();
            break;
        case IDM_TAB_PREV:
            if (g.ntabs > 0)
                SendMessageW(g.tab, TCM_SETCURSEL, (yt_app_cur_tab() + g.ntabs - 1) % g.ntabs, 0), yt_app_layout();
            break;
        case IDM_NEW_WINDOW: {
            wchar_t exe[YT_PATH_MAX];
            GetModuleFileNameW(NULL, exe, YT_PATH_MAX);
            ShellExecuteW(hwnd, L"open", exe, NULL, NULL, SW_SHOWNORMAL);
            break;
        }
        case IDM_HOTKEYS:
            MessageBoxW(hwnd,
                L"Ctrl+Shift+N  快速连接\nCtrl+W  关闭标签\nCtrl+R  重连\n"
                L"Ctrl+F  查找  F3  下一个\nCtrl+Tab  下一标签\nF11  全屏\n"
                L"Ctrl+Shift+C  复制  Ctrl+V  粘贴\nCtrl+=/-  缩放字体",
                L"快捷键", MB_OK);
            break;
        case IDM_ZRX: {
            int cur = yt_app_cur_tab();
            if (cur >= 0) yt_zmodem_rx(g.sess[cur], yt_paths()->logs);
            break;
        }
        case IDM_ZTX: {
            int cur = yt_app_cur_tab();
            OPENFILENAMEW ofn = {0}; wchar_t file[YT_PATH_MAX] = {0};
            if (cur < 0) break;
            ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFile = file; ofn.nMaxFile = YT_PATH_MAX;
            if (GetOpenFileNameW(&ofn)) yt_zmodem_tx(g.sess[cur], file);
            break;
        }
        case IDM_LAYOUT_SAVE:
            save_window_cfg();
            yt_app_set_status(L"已写入 config\\window.json 与 sessions.json");
            break;
        case IDM_LAYOUT_LOAD: {
            YtWindowCfg c;
            if (!yt_window_load(&c)) { yt_app_set_status(L"无 window.json"); break; }
            g.split_mode = c.split;
            g.sm_visible = c.sm_visible;
            g.bb_visible = c.bb_visible;
            g.sftp_visible = c.sftp_visible;
            g.theme = c.theme;
            g.font_px = c.font_px > 0 ? c.font_px : 15;
            g.stats_on = c.stats;
            yt_set_paste_delay(c.paste_delay_ms);
            apply_font_all(g.font_px);
            if (c.opacity > 0 && c.opacity < 255) apply_opacity((c.opacity * 100) / 255);
            SendMessageW(g.cmb, CB_SETCURSEL, (WPARAM)c.kind, 0);
            if (c.w > 0 && c.h > 0 && !g.fullscreen)
                SetWindowPos(hwnd, NULL, c.x, c.y, c.w, c.h, SWP_NOZORDER);
            yt_app_layout();
            yt_app_set_status(L"已恢复窗口配置");
            break;
        }
        case IDM_TAB_CLOSE_OTHER: {
            int cur = yt_app_cur_tab(), i;
            for (i = g.ntabs - 1; i >= 0; i--) if (i != cur) yt_app_close_tab(i);
            break;
        }
        case IDM_TAB_COLOR: {
            int cur = yt_app_cur_tab();
            static const wchar_t *pref[] = { L"● ", L"◆ ", L"■ ", L"▲ ", L"" };
            wchar_t title[YT_TITLE_MAX]; TCITEMW ti = {0};
            if (cur < 0) break;
            g.tab_color_cycle = (g.tab_color_cycle + 1) % 5;
            swprintf(title, YT_TITLE_MAX, L"%ls%ls", pref[g.tab_color_cycle],
                     g.sess[cur]->name[0] ? g.sess[cur]->name : g.sess[cur]->title);
            ti.mask = TCIF_TEXT; ti.pszText = title;
            SendMessageW(g.tab, TCM_SETITEMW, cur, (LPARAM)&ti);
            break;
        }
        case IDM_MACRO_REC: yt_macro_rec_start(); yt_app_set_status(L"宏录制中"); break;
        case IDM_MACRO_STOP: yt_macro_rec_stop(); yt_app_set_status(L"宏录制结束"); break;
        case IDM_MACRO_RUN: {
            int cur = yt_app_cur_tab(), i, n = yt_macro_count();
            if (cur < 0) break;
            for (i = 0; i < n; i++) send_quick_text(yt_macro_get(i));
            yt_app_set_status(L"宏已执行");
            break;
        }
        case IDM_MACRO_SAVE: yt_app_set_status(yt_macro_save() ? L"宏已保存" : L"宏保存失败"); break;
        case IDM_SCRIPT: {
            int cur = yt_app_cur_tab();
            OPENFILENAMEW ofn = {0}; wchar_t file[YT_PATH_MAX] = {0};
            if (cur < 0) break;
            ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFile = file; ofn.nMaxFile = YT_PATH_MAX;
            ofn.lpstrFilter = L"Script\0*.txt;*.sh;*.yt\0All\0*.*\0";
            ofn.Flags = OFN_FILEMUSTEXIST;
            if (GetOpenFileNameW(&ofn))
                yt_app_set_status(yt_script_run_file(g.sess[cur], file) ? L"脚本完成" : L"脚本失败");
            break;
        }
        case IDM_COMPLETE: {
            wchar_t pref[256], out[256];
            GetWindowTextW(g.quick, pref, 256);
            if (yt_hist_complete(pref, out, 256)) SetWindowTextW(g.quick, out);
            else yt_app_set_status(L"无匹配历史");
            break;
        }
        case IDM_DUMP: {
            int cur = yt_app_cur_tab();
            wchar_t path[YT_PATH_MAX];
            if (cur < 0) break;
            swprintf(path, YT_PATH_MAX, L"%ls\\screen_%d.txt", yt_paths()->logs, cur);
            yt_app_set_status(term_dump(g.terms[cur], path) ? L"屏幕已导出" : L"导出失败");
            break;
        }
        case IDM_UNIX: {
            wchar_t dir[YT_PATH_MAX];
            swprintf(dir, YT_PATH_MAX, L"%ls\\unix", yt_paths()->runtime);
            CreateDirectoryW(dir, NULL);
            ShellExecuteW(hwnd, L"open", dir, NULL, NULL, SW_SHOWNORMAL);
            break;
        }
        case IDM_THEME: {
            static int tog; tog = !tog;
            if (g.brush_chrome) DeleteObject(g.brush_chrome);
            g.brush_chrome = CreateSolidBrush(tog ? RGB(230, 235, 240) : COL_CHROME);
            InvalidateRect(hwnd, NULL, TRUE);
            break;
        }
        case IDM_COM_ENUM: {
            wchar_t names[32][32]; int n = yt_com_enum(names, 32);
            if (n > 0) { SetWindowTextW(g.com, names[0]); yt_app_set_status(names[0]); }
            else yt_app_set_status(L"未发现串口");
            break;
        }
        case IDM_STATS:
            g.stats_on = !g.stats_on;
            if (g.stats_on) SetTimer(hwnd, IDT_STATS, 1000, NULL);
            else KillTimer(hwnd, IDT_STATS);
            refresh_status_bar();
            break;
        case IDM_X11:
            yt_app_set_status(yt_x11_ensure() ? L"X11 已就绪" : L"请将 vcxsrv 放入 runtime\\x11\\");
            break;
        case IDM_ABOUT:
            MessageBoxW(hwnd, L"YTerminal " YT_VERSION L"\nSecureCRT 风格工作台\nSSH / SFTP / 串口 / 本地 / Telnet", L"关于", MB_OK);
            break;
        case IDM_EXIT: DestroyWindow(hwnd); break;
        }
        return 0;
    case WM_YT_SSH_DONE: {
        YtSession *s = (YtSession *)lp;
        BOOL ok = (BOOL)wp;
        if (!ok) {
            wchar_t w[300];
            int idx = s->tab_index;
            MultiByteToWideChar(CP_UTF8, 0, s->err[0] ? s->err : "SSH failed", -1, w, 300);
            yt_app_set_status(w);
            if (idx >= 0 && idx < g.ntabs && g.sess[idx] == s) yt_app_close_tab(idx);
            else yt_session_free(s);
        } else {
            TCITEMW ti = {0};
            ti.mask = TCIF_TEXT;
            ti.pszText = s->title;
            SendMessageW(g.tab, TCM_SETITEMW, s->tab_index, (LPARAM)&ti);
            g.sftp_visible = TRUE;
            yt_app_layout();
            yt_app_set_status(L"已连接");
            ui_bridge_log("ssh ok");
        }
        return 0;
    }
    case WM_YT_CLOSED: yt_app_set_status(L"会话已断开"); return 0;
    case WM_YT_FWD_EVENT: yt_app_set_status(L"端口转发活动"); return 0;
    case WM_YT_SFTP_REFRESH:
        if (g.sftp_visible) refresh_sftp();
        return 0;
    case WM_YT_SFTP_DONE:
        apply_sftp_list((SftpDoneMsg *)lp);
        return 0;
    case WM_YT_FOCUS_TERM: {
        int idx = (int)wp;
        if (idx >= 0 && idx < g.ntabs && g.terms[idx] && IsWindowVisible(g.terms[idx]))
            term_set_focus(g.terms[idx]);
        return 0;
    }
    case WM_DESTROY:
        save_window_cfg();
        while (g.ntabs > 0) yt_app_close_tab(0);
        ui_bridge_stop();
        if (g.font_ui) DeleteObject(g.font_ui);
        if (g.font_ui_bold) DeleteObject(g.font_ui_bold);
        if (g.brush_chrome) DeleteObject(g.brush_chrome);
        if (g.brush_cap) DeleteObject(g.brush_cap);
        if (g.brush_white) DeleteObject(g.brush_white);
        if (g.brush_status) DeleteObject(g.brush_status);
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_F11) { toggle_fullscreen(hwnd); return 0; }
        if (wp == VK_F3) { int c = yt_app_cur_tab(); if (c >= 0) term_find_next(g.terms[c]); return 0; }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE prev, PWSTR cmd, int show)
{
    WNDCLASSW wc = {0};
    HWND hwnd;
    MSG msg;
    YtPaths paths;
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_TREEVIEW_CLASSES };
    (void)prev; (void)cmd;
    InitCommonControlsEx(&icc);
    yt_paths_init(&paths);
    g.hi = hi;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"YTerminalMain";
    wc.hbrBackground = CreateSolidBrush(COL_CHROME);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassW(&wc);
    {
        YtWindowCfg wcfg;
        int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 1360, h = 820;
        if (yt_window_load(&wcfg) && wcfg.w > 0 && wcfg.h > 0) {
            x = wcfg.x; y = wcfg.y; w = wcfg.w; h = wcfg.h;
        }
        hwnd = CreateWindowExW(0, L"YTerminalMain", L"YTerminal " YT_VERSION,
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
                               x, y, w, h, NULL, build_menu(), hi, NULL);
    }
    g.hwnd = hwnd;
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (msg.wParam == 'W') { PostMessageW(hwnd, WM_COMMAND, IDM_CLOSE_TAB, 0); continue; }
            if (msg.wParam == 'R') { PostMessageW(hwnd, WM_COMMAND, IDM_RECONNECT, 0); continue; }
            if (msg.wParam == VK_TAB) {
                PostMessageW(hwnd, WM_COMMAND,
                             (GetKeyState(VK_SHIFT) & 0x8000) ? IDM_TAB_PREV : IDM_TAB_NEXT, 0);
                continue;
            }
            if (msg.wParam == VK_OEM_PLUS || msg.wParam == VK_ADD)
                { PostMessageW(hwnd, WM_COMMAND, IDM_ZOOM_IN, 0); continue; }
            if (msg.wParam == VK_OEM_MINUS || msg.wParam == VK_SUBTRACT)
                { PostMessageW(hwnd, WM_COMMAND, IDM_ZOOM_OUT, 0); continue; }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
