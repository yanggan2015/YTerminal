/* YTerminal - ui_palette.c : 全局命令面板（Ctrl+Shift+P） */
#include "app.h"
#include "ids.h"
#include "core.h"
#include "ui_term.h"
#include <windowsx.h>
#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { UINT id; const wchar_t *name; const wchar_t *hint; } PalCmd;

static const PalCmd g_cmds[] = {
    { IDC_NEW_SSH,        L"新建 SSH 会话",    L"会话" },
    { IDC_NEW_TAB,        L"新建本地终端",     L"会话" },
    { IDC_QUICK_CONNECT,  L"快速连接",         L"会话" },
    { IDC_DUPLICATE,      L"复制当前会话",     L"会话" },
    { IDC_RENAME_TAB,     L"重命名标签",       L"会话" },
    { IDC_CLOSE_TAB,      L"关闭当前标签",     L"Ctrl+W" },
    { IDC_IMPORT,         L"导入会话",         L"CRT/Moba/PuTTY/ssh" },
    { IDC_SFTP,           L"打开 SFTP 传输",   L"Ctrl+Shift+S" },
    { IDC_SPLIT_V,        L"左右分屏",         L"Ctrl+Shift+E" },
    { IDC_SPLIT_H,        L"上下分屏",         L"Ctrl+Shift+D" },
    { IDC_BROADCAST,      L"切换广播输入",     L"输入" },
    { IDC_FIND,           L"查找文本",         L"Ctrl+F" },
    { IDC_LOG,            L"切换日志记录",     L"Ctrl+Shift+L" },
    { IDC_RECONNECT,      L"重新连接",         L"连接" },
    { IDC_QUICKCMD,       L"快捷命令与规则",   L"Ctrl+Shift+K" },
    { IDC_SETTINGS,       L"打开设置",         L"配置" },
    { IDC_TOGGLE_SIDEBAR, L"显示/隐藏侧栏",    L"布局" },
    { IDC_FONT_INC,       L"放大字号",         L"Ctrl+=" },
    { IDC_FONT_DEC,       L"缩小字号",         L"Ctrl+-" },
    { IDC_FONT_RESET,     L"重置字号",         L"字号" },
    { IDC_CLEAR,          L"清屏",             L"终端" },
    { IDC_COPY,           L"复制选区",         L"Ctrl+Shift+C" },
    { IDC_PASTE,          L"粘贴",             L"Ctrl+Shift+V" },
    { IDC_SELECTALL,      L"全选",             L"终端" },
    { IDC_TUNNELS,       L"端口转发…",       L"连接" },
    { IDC_SHORTCUTS,      L"快捷键一览",       L"帮助" },
    { IDC_ABOUT,          L"关于 YTerminal",   L"帮助" },
};
#define CMD_COUNT ((int)(sizeof(g_cmds) / sizeof(g_cmds[0])))

#define PAL_SESSION_BASE 20000
#define PAL_W app_dpi(620)
#define PAL_H app_dpi(446)
#define PAL_PAD app_dpi(14)

typedef struct { UINT id; wchar_t label[96]; wchar_t hint[48]; int score; } PalItem;

typedef struct {
    HWND hwnd, edit, list, owner;
    HBRUSH bg, listbg;
    int count;
} Palette;

static PalItem g_items[300];
static int g_pal_exec = 0;
static WNDPROC g_edit_old = NULL;

static Palette *pal_from(HWND h) { return (Palette *)GetWindowLongPtrW(h, GWLP_USERDATA); }

static void pal_query(Palette *p, char *out, int cch) {
    wchar_t qw[160] = { 0 };
    GetWindowTextW(p->edit, qw, 159);
    char *u = wide_to_utf8(qw);
    _snprintf(out, (size_t)cch, "%s", u ? u : "");
    out[cch - 1] = 0;
    free(u);
}

static int pal_add(Palette *p, UINT id, const wchar_t *label, const wchar_t *hint, int score) {
    if (p->count >= 300) return 0;
    PalItem *it = &g_items[p->count];
    it->id = id;
    wcsncpy(it->label, label, 94);
    it->label[94] = 0;
    wcsncpy(it->hint, hint ? hint : L"", 46);
    it->hint[46] = 0;
    it->score = score;
    SendMessageW(p->list, LB_ADDSTRING, 0, (LPARAM)it);
    p->count++;
    return 1;
}

static void pal_build(Palette *p) {
    char q[160];
    pal_query(p, q, sizeof(q));
    p->count = 0;
    SendMessageW(p->list, LB_RESETCONTENT, 0, 0);

    for (int i = 0; i < CMD_COUNT; i++) {
        char *name_u8 = wide_to_utf8(g_cmds[i].name);
        int score = yt_fuzzy_score(q, name_u8);
        free(name_u8);
        if (score) pal_add(p, g_cmds[i].id, g_cmds[i].name, g_cmds[i].hint, score + 40);
    }
    for (int i = 0; i < g_app.sessions.n; i++) {
        Session *s = &g_app.sessions.items[i];
        if (!s->name[0] && !s->host[0]) continue;
        char label[192];
        _snprintf(label, sizeof(label), "会话: %s%s%s", s->name[0] ? s->name : s->host,
                  s->host[0] && s->name[0] ? "  " : "", s->host[0] && s->name[0] ? s->host : "");
        int score = yt_fuzzy_score(q, label);
        if (!score) continue;
        wchar_t *wl = utf8_to_wide(label);
        char hint[96];
        if (s->protocol == PROTO_SERIAL)
            _snprintf(hint, sizeof(hint), "%s %d", protocol_name(s->protocol), s->baud);
        else if (s->protocol == PROTO_LOCAL)
            _snprintf(hint, sizeof(hint), "%s", protocol_name(s->protocol));
        else
            _snprintf(hint, sizeof(hint), "%s:%d", s->host, s->port);
        wchar_t *wh = utf8_to_wide(hint);
        pal_add(p, PAL_SESSION_BASE + (UINT)i, wl ? wl : L"会话", wh ? wh : L"", score);
        free(wl);
        free(wh);
    }
    for (int i = 1; i < p->count; i++) {
        PalItem tmp = g_items[i];
        int j = i - 1;
        while (j >= 0 && g_items[j].score < tmp.score) { g_items[j + 1] = g_items[j]; j--; }
        g_items[j + 1] = tmp;
    }
    if (p->count > 1) {
        SendMessageW(p->list, LB_RESETCONTENT, 0, 0);
        for (int i = 0; i < p->count; i++) SendMessageW(p->list, LB_ADDSTRING, 0, (LPARAM)&g_items[i]);
    }
    if (p->count > 0) SendMessageW(p->list, LB_SETCURSEL, 0, 0);
}

static void pal_execute(Palette *p) {
    if (p->count <= 0) return;
    LRESULT sel = SendMessageW(p->list, LB_GETCURSEL, 0, 0);
    if (sel < 0) sel = 0;
    if (sel >= p->count) return;
    g_pal_exec = (int)g_items[sel].id;
    DestroyWindow(p->hwnd);
}

static LRESULT CALLBACK pal_edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Palette *p = (Palette *)GetPropW(h, L"pal");
    if (p && msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) { pal_execute(p); return 0; }
        if (wp == VK_ESCAPE) { DestroyWindow(p->hwnd); return 0; }
        if (wp == VK_DOWN || wp == VK_UP) {
            LRESULT sel = SendMessageW(p->list, LB_GETCURSEL, 0, 0);
            if (sel < 0) sel = 0;
            sel += (wp == VK_DOWN) ? 1 : -1;
            if (sel < 0) sel = 0;
            if (sel >= p->count) sel = p->count - 1;
            SendMessageW(p->list, LB_SETCURSEL, (WPARAM)sel, 0);
            return 0;
        }
    }
    return CallWindowProcW(g_edit_old, h, msg, wp, lp);
}

static LRESULT CALLBACK PaletteProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Palette *p = pal_from(h);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        p = (Palette *)xcalloc(1, sizeof(Palette));
        p->hwnd = h;
        p->owner = cs->hwndParent;
        p->bg = CreateSolidBrush(app_cr(g_app.theme->bg, RGB(24, 24, 24)));
        p->listbg = CreateSolidBrush(app_cr(g_app.theme->ui_panel, RGB(32, 32, 32)));
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)p);
        p->edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                  PAL_PAD, PAL_PAD + app_dpi(2), PAL_W - PAL_PAD * 2, app_dpi(24),
                                  h, (HMENU)IDC_PAL_EDIT, g_app.inst, NULL);
        SendMessageW(p->edit, WM_SETFONT, (WPARAM)g_app.f_ui, TRUE);
        SendMessageW(p->edit, EM_SETCUEBANNER, TRUE, (LPARAM)L"输入命令、功能或会话名…");
        SetPropW(p->edit, L"pal", p);
        g_edit_old = (WNDPROC)SetWindowLongPtrW(p->edit, GWLP_WNDPROC, (LONG_PTR)pal_edit_proc);

        p->list = CreateWindowExW(0, L"LISTBOX", L"",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED |
                                  LBS_NOINTEGRALHEIGHT,
                                  PAL_PAD, PAL_PAD + app_dpi(38), PAL_W - PAL_PAD * 2,
                                  PAL_H - PAL_PAD * 2 - app_dpi(62),
                                  h, (HMENU)IDC_PAL_LIST, g_app.inst, NULL);
        SendMessageW(p->list, WM_SETFONT, (WPARAM)g_app.f_ui, TRUE);
        SendMessageW(p->list, LB_SETITEMHEIGHT, 0, (LPARAM)app_dpi(30));
        return 0;
    }
    case WM_DESTROY:
        if (p) {
            if (p->bg) DeleteObject(p->bg);
            if (p->listbg) DeleteObject(p->listbg);
            if (p->owner) EnableWindow(p->owner, TRUE);
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            free(p);
        }
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HDC hdc = ps.hdc;
        app_fill(hdc, &rc, app_cr(g_app.theme->ui_panel, RGB(255, 255, 255)));
        RECT top = { rc.left, rc.top, rc.right, rc.top + app_dpi(2) };
        app_fill(hdc, &top, app_cr(g_app.theme->ui_accent, RGB(0, 120, 215)));
        RECT div = { rc.left, app_dpi(54), rc.right, app_dpi(55) };
        app_fill(hdc, &div, app_cr(g_app.theme->ui_border, RGB(200, 200, 200)));
        RECT fr = { PAL_PAD, rc.bottom - app_dpi(26), rc.right - PAL_PAD, rc.bottom - app_dpi(6) };
        app_draw_text(hdc, L"Enter 执行 · ↑↓ 选择 · Esc 关闭", &fr, g_app.f_ui,
                      app_cr(g_app.theme->ui_dim, RGB(120, 120, 120)), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)wp;
        SetBkColor(dc, app_cr(g_app.theme->ui_panel, RGB(32, 32, 32)));
        SetTextColor(dc, app_cr(g_app.theme->ui_text, RGB(230, 230, 230)));
        return (LRESULT)p->listbg;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetBkColor(dc, app_cr(g_app.theme->bg, RGB(24, 24, 24)));
        SetTextColor(dc, app_cr(g_app.theme->ui_text, RGB(230, 230, 230)));
        return (LRESULT)p->bg;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp;
        if (di->CtlID != IDC_PAL_LIST) return TRUE;
        int sel = (di->itemID != (UINT)-1) && (di->itemState & ODS_SELECTED);
        RECT r = di->rcItem;
        HDC dc = di->hDC;
        app_fill(dc, &r, app_cr(sel ? g_app.theme->ui_hover : g_app.theme->ui_panel, RGB(255, 255, 255)));
        if (sel) {
            RECT bar = { r.left, r.top, r.left + app_dpi(3), r.bottom };
            app_fill(dc, &bar, app_cr(g_app.theme->ui_accent, RGB(0, 120, 215)));
        }
        LRESULT data = SendMessageW(di->hwndItem, LB_GETITEMDATA, di->itemID, 0);
        PalItem *it = (PalItem *)(INT_PTR)data;
        if (it && it != (PalItem *)(INT_PTR)-1) {
            RECT tr = r;
            tr.left += app_dpi(14);
            tr.right -= app_dpi(160);
            app_draw_text(dc, it->label, &tr, g_app.f_ui, app_cr(g_app.theme->ui_text, RGB(20, 20, 20)),
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            RECT hr = r;
            hr.left = r.right - app_dpi(156);
            hr.right -= app_dpi(14);
            app_draw_text(dc, it->hint, &hr, g_app.f_ui, app_cr(g_app.theme->ui_dim, RGB(120, 120, 120)),
                          DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_PAL_EDIT && HIWORD(wp) == EN_CHANGE) { pal_build(p); return 0; }
        if (LOWORD(wp) == IDC_PAL_LIST && HIWORD(wp) == LBN_DBLCLK) { pal_execute(p); return 0; }
        break;
    case WM_SIZE:
        if (p) {
            RECT rc;
            GetClientRect(h, &rc);
            MoveWindow(p->edit, PAL_PAD, PAL_PAD + app_dpi(2), rc.right - PAL_PAD * 2, app_dpi(24), TRUE);
            MoveWindow(p->list, PAL_PAD, PAL_PAD + app_dpi(38), rc.right - PAL_PAD * 2,
                       rc.bottom - (PAL_PAD + app_dpi(38)) - app_dpi(34), TRUE);
        }
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void app_show_palette(HWND parent) {
    static int registered = 0;
    if (!registered) {
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = PaletteProc;
        wc.hInstance = g_app.inst;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = L"YPalette";
        RegisterClassExW(&wc);
        registered = 1;
    }
    RECT orc = { 0, 0, 0, 0 };
    if (parent) GetWindowRect(parent, &orc);
    int x = orc.left + ((orc.right - orc.left) - PAL_W) / 2;
    int y = orc.top + app_dpi(90);
    if (y < 0) y = app_dpi(60);

    g_pal_exec = 0;
    HWND dlg = CreateWindowExW(WS_EX_TOOLWINDOW, L"YPalette", L"命令面板",
                               WS_POPUP | WS_BORDER, x, y, PAL_W, PAL_H,
                               parent, NULL, g_app.inst, NULL);
    if (!dlg) return;
    Palette *p = pal_from(dlg);
    if (!p) { DestroyWindow(dlg); return; }

    EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);
    SetFocus(p->edit);
    pal_build(p);

    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (parent) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }
    if (g_pal_exec) {
        if (g_pal_exec >= PAL_SESSION_BASE) app_connect_session(g_pal_exec - PAL_SESSION_BASE);
        else PostMessageW(g_app.main, WM_COMMAND, (WPARAM)g_pal_exec, 0);
    }
}
