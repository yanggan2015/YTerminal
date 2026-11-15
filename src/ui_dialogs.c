#include "app.h"
#include "ids.h"
#include "core.h"
#include "ui_term.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* 通用                                                                */
/* ================================================================== */
typedef struct {
    HWND hwnd;
    HBRUSH bg, editbg;
    int result;
} DlgBase;


static HFONT dfont(void) { return g_app.f_ui ? g_app.f_ui : (HFONT)GetStockObject(DEFAULT_GUI_FONT); }

static HWND mk_label(HWND p, const wchar_t *text, int x, int y, int w) {
    HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                             x, y, w, 20, p, NULL, g_app.inst, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)dfont(), TRUE);
    return h;
}

static HWND mk_edit(HWND p, int id, const wchar_t *val, int x, int y, int w, int h, DWORD extra) {
    HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", val ? val : L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extra,
                             x, y, w, h, p, (HMENU)(INT_PTR)id, g_app.inst, NULL);
    SendMessageW(e, WM_SETFONT, (WPARAM)dfont(), TRUE);
    return e;
}

static HWND mk_combo(HWND p, int id, int x, int y, int w, int h) {
    HWND c = CreateWindowExW(0, L"COMBOBOX", L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                             x, y, w, h, p, (HMENU)(INT_PTR)id, g_app.inst, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)dfont(), TRUE);
    return c;
}

static HWND mk_check(HWND p, int id, const wchar_t *text, int x, int y, int w, int checked) {
    HWND c = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                             x, y, w, 22, p, (HMENU)(INT_PTR)id, g_app.inst, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)dfont(), TRUE);
    SendMessageW(c, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return c;
}

static HWND mk_button(HWND p, int id, const wchar_t *text, int x, int y, int w, int h, int def) {
    HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | (def ? BS_DEFPUSHBUTTON : 0),
                             x, y, w, h, p, (HMENU)(INT_PTR)id, g_app.inst, NULL);
    SendMessageW(b, WM_SETFONT, (WPARAM)dfont(), TRUE);
    return b;
}

static void wset(HWND p, int id, const wchar_t *text) { SetDlgItemTextW(p, id, text ? text : L""); }
static void wget(HWND p, int id, char *buf, int cch, const char *def) {
    wchar_t wt[1024] = { 0 };
    GetDlgItemTextW(p, id, wt, 1024);
    char *u = wide_to_utf8(wt);
    if (u && *u) { strncpy(buf, u, cch - 1); buf[cch - 1] = 0; }
    else if (def) { strncpy(buf, def, cch - 1); buf[cch - 1] = 0; }
    else buf[0] = 0;
    free(u);
}
static int wint(HWND p, int id, int def) {
    wchar_t wt[64] = { 0 };
    GetDlgItemTextW(p, id, wt, 64);
    int v = _wtoi(wt);
    return v ? v : def;
}

static int modal_run(HWND dlg, int *result) {
    HWND owner = GetWindow(dlg, GW_OWNER);
    if (owner) {
        RECT orc, drc;
        GetWindowRect(owner, &orc);
        GetWindowRect(dlg, &drc);
        int w = drc.right - drc.left, h = drc.bottom - drc.top;
        SetWindowPos(dlg, NULL, orc.left + ((orc.right - orc.left) - w) / 2,
                     orc.top + ((orc.bottom - orc.top) - h) / 2, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER);
        EnableWindow(owner, FALSE);
    }
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);
    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner) { EnableWindow(owner, TRUE); SetForegroundWindow(owner); }
    int r = 0;
    if (result) r = *result;
    return r;
}

/* ================================================================== */
/* 会话编辑                                                             */
/* ================================================================== */
typedef struct {
    DlgBase b;
    Session s;
    int index;
    int layout_bottom;
    HWND c_pages[4];
} SessionDlg;

static void sd_show_band(HWND h, int y0, int y1, int show) {
    HWND c = GetWindow(h, GW_CHILD);
    while (c) {
        RECT r; GetWindowRect(c, &r);
        POINT pt = { r.left, r.top };
        ScreenToClient(h, &pt);
        if (pt.y >= y0 && pt.y < y1 && pt.y > app_dpi(112)) ShowWindow(c, show ? SW_SHOW : SW_HIDE);
        c = GetWindow(c, GW_HWNDNEXT);
    }
}

/* 根据协议收紧下半部分：快捷命令框与按钮跟随内容底部 */
static void sd_relayout(SessionDlg *sd) {
    HWND h = sd->b.hwnd;
    int bottom = sd->layout_bottom;
    int qy = bottom + 28;
    int by = bottom + 126;
    HWND q = GetDlgItem(h, IDC_DLG_QUICKCMDS);
    if (q) MoveWindow(q, app_dpi(18), app_dpi(qy), app_dpi(582), app_dpi(96), TRUE);
    struct { int id; int x; int w; } btns[] = {
        { IDC_DLG_TEST, 330, 120 }, { IDC_DLG_OK, 458, 72 }, { IDC_DLG_CANCEL, 534, 64 }
    };
    for (int i = 0; i < 3; i++) {
        HWND b = GetDlgItem(h, btns[i].id);
        if (b) MoveWindow(b, app_dpi(btns[i].x), app_dpi(by), app_dpi(btns[i].w), app_dpi(30), TRUE);
    }
    /* 窗口高度要扣掉非客户区（标题栏+边框），否则底部按钮会被裁掉 */
    RECT wr, cr;
    GetWindowRect(h, &wr);
    GetClientRect(h, &cr);
    int dw = (wr.right - wr.left) - cr.right;
    int dh = (wr.bottom - wr.top) - cr.bottom;
    SetWindowPos(h, NULL, 0, 0, app_dpi(636) + dw, app_dpi(bottom + 214) + dh,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(h, NULL, TRUE);
}

static void sd_sync_proto(SessionDlg *sd, int proto) {
    int show_ssh = proto == PROTO_SSH || proto == PROTO_TELNET;
    int show_ser = proto == PROTO_SERIAL;
    int show_loc = proto == PROTO_LOCAL;
    sd_show_band(sd->b.hwnd, 0, app_dpi(112), 1);
    sd_show_band(sd->b.hwnd, app_dpi(112), app_dpi(336), show_ssh);
    sd_show_band(sd->b.hwnd, app_dpi(336), app_dpi(476), show_ser);
    sd_show_band(sd->b.hwnd, app_dpi(476), app_dpi(576), show_loc);
    sd_show_band(sd->b.hwnd, app_dpi(576), app_dpi(760), 1);
    /* 标签文字显隐由 WM_PAINT 根据协议处理，这里只处理控件 */
    sd->layout_bottom = (proto == PROTO_SERIAL) ? 476 : (proto == PROTO_LOCAL) ? 576 : 336;
    sd_relayout(sd);
    InvalidateRect(sd->b.hwnd, NULL, TRUE);
}
static void sd_load(SessionDlg *sd) {
    Session *s = &sd->s;
    wset(sd->b.hwnd, IDC_DLG_NAME, (wchar_t *)utf8_to_wide(s->name));
    wset(sd->b.hwnd, IDC_DLG_GROUP, (wchar_t *)utf8_to_wide(s->group));
    SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_PROTO, CB_SETCURSEL, s->protocol, 0);
    SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_ENC, CB_SETCURSEL, s->encoding, 0);
    wset(sd->b.hwnd, IDC_DLG_HOST, (wchar_t *)utf8_to_wide(s->host));
    wchar_t buf[32];
    _snwprintf(buf, 32, L"%d", s->port); buf[31] = 0;
    wset(sd->b.hwnd, IDC_DLG_PORT, buf);
    wset(sd->b.hwnd, IDC_DLG_USER, (wchar_t *)utf8_to_wide(s->user));
    SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_AUTH, CB_SETCURSEL, s->auth, 0);
    wset(sd->b.hwnd, IDC_DLG_PASS, (wchar_t *)utf8_to_wide(s->password));
    wset(sd->b.hwnd, IDC_DLG_KEY, (wchar_t *)utf8_to_wide(s->keyfile));
    _snwprintf(buf, 32, L"%d", s->keepalive); buf[31] = 0;
    wset(sd->b.hwnd, IDC_DLG_KEEPALIVE, buf);
    wset(sd->b.hwnd, IDC_DLG_EXTRA, (wchar_t *)utf8_to_wide(s->extra_args));
    wset(sd->b.hwnd, IDC_DLG_SERIALPORT, (wchar_t *)utf8_to_wide(s->port_name));
    _snwprintf(buf, 32, L"%d", s->baud ? s->baud : 115200); buf[31] = 0;
    wset(sd->b.hwnd, IDC_DLG_BAUD, buf);
    SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_DATABITS, CB_SETCURSEL, s->databits == 8 ? 0 : s->databits == 7 ? 1 : 2, 0);
    SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_PARITY, CB_SETCURSEL, s->parity, 0);
    SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_STOPBITS, CB_SETCURSEL, s->stopbits == 2 ? 1 : 0, 0);
    SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_FLOWX, CB_SETCURSEL, s->flow == 2 ? 2 : s->flow == 1 ? 0 : 1, 0);
    if (s->dtr) SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_DTR, BM_SETCHECK, BST_CHECKED, 0);
    if (s->rts) SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_RTS, BM_SETCHECK, BST_CHECKED, 0);
    wset(sd->b.hwnd, IDC_DLG_SHELL, (wchar_t *)utf8_to_wide(s->shell));
    wset(sd->b.hwnd, IDC_DLG_CWD, s->cwd);
    wset(sd->b.hwnd, IDC_DLG_QUICKCMDS, (wchar_t *)utf8_to_wide(s->quick_cmds));
    sd_sync_proto(sd, s->protocol);
}

static void sd_collect(SessionDlg *sd) {
    Session *s = &sd->s;
    wget(sd->b.hwnd, IDC_DLG_NAME, s->name, sizeof(s->name), "新会话");
    wget(sd->b.hwnd, IDC_DLG_GROUP, s->group, sizeof(s->group), "默认");
    s->protocol = (int)SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_PROTO, CB_GETCURSEL, 0, 0);
    int enc_sel = (int)SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_ENC, CB_GETCURSEL, 0, 0);
    s->encoding = enc_sel >= 0 ? enc_sel : 0;
    wget(sd->b.hwnd, IDC_DLG_HOST, s->host, sizeof(s->host), "");
    s->port = wint(sd->b.hwnd, IDC_DLG_PORT, s->protocol == PROTO_TELNET ? 23 : 22);
    wget(sd->b.hwnd, IDC_DLG_USER, s->user, sizeof(s->user), "");
    s->auth = (int)SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_AUTH, CB_GETCURSEL, 0, 0);
    wget(sd->b.hwnd, IDC_DLG_PASS, s->password, sizeof(s->password), "");
    wget(sd->b.hwnd, IDC_DLG_KEY, s->keyfile, sizeof(s->keyfile), "");
    s->keepalive = wint(sd->b.hwnd, IDC_DLG_KEEPALIVE, 30);
    wget(sd->b.hwnd, IDC_DLG_EXTRA, s->extra_args, sizeof(s->extra_args), "");
    wget(sd->b.hwnd, IDC_DLG_SERIALPORT, s->port_name, sizeof(s->port_name), "COM1");
    s->baud = wint(sd->b.hwnd, IDC_DLG_BAUD, 115200);
    int db = (int)SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_DATABITS, CB_GETCURSEL, 0, 0);
    s->databits = db == 0 ? 8 : db == 1 ? 7 : 6;
    s->parity = (int)SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_PARITY, CB_GETCURSEL, 0, 0);
    s->stopbits = (int)SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_STOPBITS, CB_GETCURSEL, 0, 0) == 1 ? 2 : 1;
    int fl = (int)SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_FLOWX, CB_GETCURSEL, 0, 0);
    s->flow = fl == 0 ? 1 : fl == 1 ? 0 : 2;
    s->dtr = SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_DTR, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s->rts = SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_RTS, BM_GETCHECK, 0, 0) == BST_CHECKED;
    wget(sd->b.hwnd, IDC_DLG_SHELL, s->shell, sizeof(s->shell), "powershell.exe");
    wget(sd->b.hwnd, IDC_DLG_QUICKCMDS, s->quick_cmds, sizeof(s->quick_cmds), "");
}

static void sd_browse_key(SessionDlg *sd) {
    wchar_t path[MAX_PATH * 2] = { 0 };
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = sd->b.hwnd;
    ofn.lpstrFilter = L"私钥文件\0*.pem;*.key;id_*;*.ppk\0所有文件\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) wset(sd->b.hwnd, IDC_DLG_KEY, path);
}

static void sd_browse_dir(SessionDlg *sd) {
    wchar_t path[MAX_PATH * 2] = { 0 };
    BROWSEINFOW bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = sd->b.hwnd;
    bi.lpszTitle = L"选择工作目录";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST idl = SHBrowseForFolderW(&bi);
    if (idl) {
        SHGetPathFromIDListW(idl, path);
        CoTaskMemFree(idl);
        if (path[0]) wset(sd->b.hwnd, IDC_DLG_CWD, path);
    }
}

static void sd_browse_shell(SessionDlg *sd) {
    wchar_t path[MAX_PATH * 2] = { 0 };
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = sd->b.hwnd;
    ofn.lpstrFilter = L"程序\0*.exe\0所有文件\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) wset(sd->b.hwnd, IDC_DLG_SHELL, path);
}

static void sd_save(SessionDlg *sd, int connect) {
    sd_collect(sd);
    if (!sd->s.name[0]) { strncpy(sd->s.name, sd->s.host[0] ? sd->s.host : "新会话", sizeof(sd->s.name) - 1); }
    if (sd->index >= 0) {
        Session *old = &g_app.sessions.items[sd->index];
        char id[48];
        strncpy(id, old->id, 48);
        sd->s = *old;
        sd_collect(sd);
        strncpy(sd->s.id, id, sizeof(sd->s.id) - 1);
        *old = sd->s;
    } else {
        session_generate_id(&sd->s);
        sessions_add(&g_app.sessions, &sd->s);
    }
    sessions_save(&g_app.sessions);
    PostMessageW(g_app.main, WM_YT_SIDEBAR_REFRESH, 0, 0);
    sd->b.result = 1;
    DestroyWindow(sd->b.hwnd);
    if (connect) app_open_session(&sd->s, 1);
}

static void sd_on_command(SessionDlg *sd, int id, int code) {
    switch (id) {
    case IDC_DLG_PROTO:
        if (code == CBN_SELCHANGE) {
            int p = (int)SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_PROTO, CB_GETCURSEL, 0, 0);
            sd_sync_proto(sd, p);
            if (p == PROTO_SSH) { if (!wint(sd->b.hwnd, IDC_DLG_PORT, 0)) wset(sd->b.hwnd, IDC_DLG_PORT, L"22"); }
            if (p == PROTO_TELNET) wset(sd->b.hwnd, IDC_DLG_PORT, L"23");
            InvalidateRect(sd->b.hwnd, NULL, TRUE);
        }
        break;
    case IDC_DLG_AUTH: {
        int a = (int)SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_AUTH, CB_GETCURSEL, 0, 0);
        EnableWindow(GetDlgItem(sd->b.hwnd, IDC_DLG_PASS), a == AUTH_PASSWORD || a == AUTH_KEYBOARD);
        EnableWindow(GetDlgItem(sd->b.hwnd, IDC_DLG_KEY), a == AUTH_KEY);
        EnableWindow(GetDlgItem(sd->b.hwnd, IDC_DLG_KEYBROWSE), a == AUTH_KEY);
        break;
    }
    case IDC_DLG_KEYBROWSE: sd_browse_key(sd); break;
    case IDC_DLG_CWDBROWSE: sd_browse_dir(sd); break;
    case IDC_DLG_QSHELL: sd_browse_shell(sd); break;
    case IDC_DLG_OK: sd_save(sd, 0); break;
    case IDC_DLG_TEST: sd_save(sd, 1); break;
    case IDC_DLG_CANCEL: DestroyWindow(sd->b.hwnd); break;
    }
}

static void sd_paint(SessionDlg *sd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(sd->b.hwnd, &ps);
    RECT rc; GetClientRect(sd->b.hwnd, &rc);
    app_fill(hdc, &rc, g_app.theme->ui_bg);
    int proto = (int)SendDlgItemMessageW(sd->b.hwnd, IDC_DLG_PROTO, CB_GETCURSEL, 0, 0);
    const wchar_t *group;
    if (proto == PROTO_SERIAL) group = L"串口参数";
    else if (proto == PROTO_LOCAL) group = L"本地 Shell";
    else group = L"连接参数";
    RECT gr = { app_dpi(18), app_dpi(112), rc.right - app_dpi(18), app_dpi(132) };
    app_draw_text(hdc, group, &gr, g_app.f_ui_b, g_app.theme->ui_accent, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT line = { gr.left, app_dpi(132), gr.right, app_dpi(133) };
    app_fill(hdc, &line, g_app.theme->ui_border);
    RECT qr = { app_dpi(18), app_dpi(sd->layout_bottom + 4), rc.right - app_dpi(18), app_dpi(sd->layout_bottom + 24) };
    app_draw_text(hdc, L"快捷命令（每行一条：命令|说明）", &qr, g_app.f_ui, g_app.theme->ui_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    EndPaint(sd->b.hwnd, &ps);
}

static LRESULT CALLBACK SessionProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    SessionDlg *sd = (SessionDlg *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        sd = (SessionDlg *)cs->lpCreateParams;
        sd->b.hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)sd);
        int x = app_dpi(100), w = app_dpi(470), y = app_dpi(16), lh = app_dpi(32);
        int lx = app_dpi(18);
        mk_label(h, L"协议", lx, y + 3, app_dpi(72));
        HWND pc = mk_combo(h, IDC_DLG_PROTO, x, y, app_dpi(180), app_dpi(200));
        SendMessageW(pc, CB_ADDSTRING, 0, (LPARAM)L"SSH");
        SendMessageW(pc, CB_ADDSTRING, 0, (LPARAM)L"串口");
        SendMessageW(pc, CB_ADDSTRING, 0, (LPARAM)L"Telnet");
        SendMessageW(pc, CB_ADDSTRING, 0, (LPARAM)L"本地 Shell");
        y += lh;
        mk_label(h, L"名称", lx, y + 3, app_dpi(72));
        mk_edit(h, IDC_DLG_NAME, L"", x, y, w, app_dpi(24), 0);
        y += lh;
        mk_label(h, L"分组", lx, y + 3, app_dpi(72));
        mk_edit(h, IDC_DLG_GROUP, L"", x, y, app_dpi(270), app_dpi(24), 0);
        mk_label(h, L"编码", x + app_dpi(282), y + 3, app_dpi(36));
        HWND eenc = mk_combo(h, IDC_DLG_ENC, x + app_dpi(320), y, app_dpi(150), app_dpi(200));
        SendMessageW(eenc, CB_ADDSTRING, 0, (LPARAM)L"UTF-8");
        SendMessageW(eenc, CB_ADDSTRING, 0, (LPARAM)L"GBK");
        SendMessageW(eenc, CB_ADDSTRING, 0, (LPARAM)L"BIG5");
        SendMessageW(eenc, CB_ADDSTRING, 0, (LPARAM)L"Shift-JIS");
        SendMessageW(eenc, CB_ADDSTRING, 0, (LPARAM)L"EUC-KR");
        SendMessageW(eenc, CB_SETCURSEL, 0, 0);

        /* ---- SSH / Telnet 区（120..330） ---- */
        int sy = app_dpi(126);
        mk_label(h, L"主机", lx, sy + 3, app_dpi(72));
        mk_edit(h, IDC_DLG_HOST, L"", x, sy, app_dpi(320), app_dpi(24), 0);
        mk_label(h, L"端口", x + app_dpi(330), sy + 3, app_dpi(36));
        mk_edit(h, IDC_DLG_PORT, L"22", x + app_dpi(368), sy, app_dpi(100), app_dpi(24), ES_NUMBER);
        sy += lh;
        mk_label(h, L"用户名", lx, sy + 3, app_dpi(72));
        mk_edit(h, IDC_DLG_USER, L"", x, sy, app_dpi(200), app_dpi(24), 0);
        mk_label(h, L"认证", x + app_dpi(210), sy + 3, app_dpi(40));
        HWND ac = mk_combo(h, IDC_DLG_AUTH, x + app_dpi(252), sy, app_dpi(216), app_dpi(200));
        SendMessageW(ac, CB_ADDSTRING, 0, (LPARAM)L"密码");
        SendMessageW(ac, CB_ADDSTRING, 0, (LPARAM)L"私钥");
        SendMessageW(ac, CB_ADDSTRING, 0, (LPARAM)L"SSH Agent");
        SendMessageW(ac, CB_ADDSTRING, 0, (LPARAM)L"键盘交互");
        sy += lh;
        mk_label(h, L"密码", lx, sy + 3, app_dpi(72));
        mk_edit(h, IDC_DLG_PASS, L"", x, sy, w, app_dpi(24), ES_PASSWORD);
        sy += lh;
        mk_label(h, L"私钥", lx, sy + 3, app_dpi(72));
        mk_edit(h, IDC_DLG_KEY, L"", x, sy, app_dpi(390), app_dpi(24), 0);
        mk_button(h, IDC_DLG_KEYBROWSE, L"浏览…", x + app_dpi(398), sy, app_dpi(72), app_dpi(24), 0);
        sy += lh;
        mk_label(h, L"保活(秒)", lx, sy + 3, app_dpi(72));
        mk_edit(h, IDC_DLG_KEEPALIVE, L"30", x, sy, app_dpi(100), app_dpi(24), ES_NUMBER);
        mk_label(h, L"附加参数", x + app_dpi(110), sy + 3, app_dpi(70));
        mk_edit(h, IDC_DLG_EXTRA, L"", x + app_dpi(182), sy, app_dpi(288), app_dpi(24), 0);

        /* ---- 串口区（340..470） ---- */
        sy = app_dpi(346);
        mk_label(h, L"串口", lx, sy + 3, app_dpi(72));
        HWND sc = mk_combo(h, IDC_DLG_SERIALPORT, x, sy, app_dpi(140), app_dpi(200));
        for (int i = 1; i <= 32; i++) {
            wchar_t name[16];
            _snwprintf(name, 16, L"COM%d", i);
            SendMessageW(sc, CB_ADDSTRING, 0, (LPARAM)name);
        }
        SendMessageW(sc, CB_SETCURSEL, 0, 0);
        mk_label(h, L"波特率", x + app_dpi(150), sy + 3, app_dpi(50));
        HWND bc = mk_combo(h, IDC_DLG_BAUD, x + app_dpi(202), sy, app_dpi(120), app_dpi(200));
        int bauds[] = { 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600 };
        for (int i = 0; i < 8; i++) { wchar_t b[16]; _snwprintf(b, 16, L"%d", bauds[i]); SendMessageW(bc, CB_ADDSTRING, 0, (LPARAM)b); }
        SendMessageW(bc, CB_SETCURSEL, 4, 0);
        sy += lh;
        mk_label(h, L"数据位", lx, sy + 3, app_dpi(72));
        HWND dc = mk_combo(h, IDC_DLG_DATABITS, x, sy, app_dpi(80), app_dpi(200));
        SendMessageW(dc, CB_ADDSTRING, 0, (LPARAM)L"8");
        SendMessageW(dc, CB_ADDSTRING, 0, (LPARAM)L"7");
        SendMessageW(dc, CB_ADDSTRING, 0, (LPARAM)L"6");
        SendMessageW(dc, CB_SETCURSEL, 0, 0);
        mk_label(h, L"校验", x + app_dpi(92), sy + 3, app_dpi(36));
        HWND pac = mk_combo(h, IDC_DLG_PARITY, x + app_dpi(130), sy, app_dpi(90), app_dpi(200));
        SendMessageW(pac, CB_ADDSTRING, 0, (LPARAM)L"无");
        SendMessageW(pac, CB_ADDSTRING, 0, (LPARAM)L"奇");
        SendMessageW(pac, CB_ADDSTRING, 0, (LPARAM)L"偶");
        SendMessageW(pac, CB_ADDSTRING, 0, (LPARAM)L"标记");
        SendMessageW(pac, CB_ADDSTRING, 0, (LPARAM)L"空格");
        SendMessageW(pac, CB_SETCURSEL, 0, 0);
        mk_label(h, L"停止位", x + app_dpi(230), sy + 3, app_dpi(50));
        HWND stc = mk_combo(h, IDC_DLG_STOPBITS, x + app_dpi(282), sy, app_dpi(70), app_dpi(200));
        SendMessageW(stc, CB_ADDSTRING, 0, (LPARAM)L"1");
        SendMessageW(stc, CB_ADDSTRING, 0, (LPARAM)L"2");
        SendMessageW(stc, CB_SETCURSEL, 0, 0);
        sy += lh;
        mk_label(h, L"流控", lx, sy + 3, app_dpi(72));
        HWND fc = mk_combo(h, IDC_DLG_FLOWX, x, sy, app_dpi(130), app_dpi(200));
        SendMessageW(fc, CB_ADDSTRING, 0, (LPARAM)L"XON/XOFF");
        SendMessageW(fc, CB_ADDSTRING, 0, (LPARAM)L"无");
        SendMessageW(fc, CB_ADDSTRING, 0, (LPARAM)L"RTS/CTS");
        SendMessageW(fc, CB_SETCURSEL, 1, 0);
        mk_check(h, IDC_DLG_DTR, L"DTR", x + app_dpi(140), sy, app_dpi(72), 1);
        mk_check(h, IDC_DLG_RTS, L"RTS", x + app_dpi(214), sy, app_dpi(72), 1);

        /* ---- 本地 Shell 区（480..570） ---- */
        sy = app_dpi(486);
        mk_label(h, L"Shell", lx, sy + 3, app_dpi(72));
        mk_edit(h, IDC_DLG_SHELL, L"powershell.exe", x, sy, app_dpi(390), app_dpi(24), 0);
        mk_button(h, IDC_DLG_QSHELL, L"浏览…", x + app_dpi(398), sy, app_dpi(72), app_dpi(24), 0);
        sy += lh;
        mk_label(h, L"工作目录", lx, sy + 3, app_dpi(72));
        mk_edit(h, IDC_DLG_CWD, L"", x, sy, app_dpi(390), app_dpi(24), 0);
        mk_button(h, IDC_DLG_CWDBROWSE, L"浏览…", x + app_dpi(398), sy, app_dpi(72), app_dpi(24), 0);

        /* ---- 快捷命令（580 起） ---- */
        mk_edit(h, IDC_DLG_QUICKCMDS, L"", app_dpi(18), app_dpi(600), app_dpi(582), app_dpi(96),
                ES_MULTILINE | WS_VSCROLL | ES_WANTRETURN);
        mk_button(h, IDC_DLG_TEST, L"保存并连接", app_dpi(330), app_dpi(712), app_dpi(120), app_dpi(30), 1);
        mk_button(h, IDC_DLG_OK, L"保存", app_dpi(458), app_dpi(712), app_dpi(72), app_dpi(30), 0);
        mk_button(h, IDC_DLG_CANCEL, L"取消", app_dpi(534), app_dpi(712), app_dpi(64), app_dpi(30), 0);

        sd->b.bg = CreateSolidBrush(app_ui_bg());
        sd->b.editbg = CreateSolidBrush(app_ui_panel());
        sd_load(sd);
        return 0;
    }
    case WM_COMMAND: sd_on_command(sd, LOWORD(wp), HIWORD(wp)); return 0;
    case WM_PAINT: sd_paint(sd); return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, app_ui_text());
        SetBkColor(dc, app_ui_bg());
        return (LRESULT)sd->b.bg;
    }
    case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, app_ui_text());
        SetBkColor(dc, app_ui_panel());
        return (LRESULT)sd->b.editbg;
    }
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        if (sd->b.bg) DeleteObject(sd->b.bg);
        if (sd->b.editbg) DeleteObject(sd->b.editbg);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void register_dlg(const wchar_t *cls, WNDPROC proc) {
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = g_app.inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = cls;
    wc.hbrBackground = NULL;
    RegisterClassExW(&wc);
}

void app_show_session_dialog(HWND parent, int session_index) {
    register_dlg(L"YSessionDlg", SessionProc);
    SessionDlg sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.index = session_index;
    if (session_index >= 0 && session_index < g_app.sessions.n) {
        sd.s = g_app.sessions.items[session_index];
        sd.index = session_index;
    } else {
        int proto = session_index == -2 ? PROTO_LOCAL : session_index == -3 ? PROTO_SERIAL : PROTO_SSH;
        sd.s = session_defaults(proto);
        sd.index = -1;
    }
    wchar_t title[128];
    _snwprintf(title, 128, session_index >= 0 ? L"编辑会话" : L"新建会话");
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW, L"YSessionDlg", title,
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                               CW_USEDEFAULT, CW_USEDEFAULT, app_dpi(636), app_dpi(760),
                               parent, NULL, g_app.inst, &sd);
    if (!dlg) return;
    sd.b.bg = NULL; sd.b.editbg = NULL;
    modal_run(dlg, &sd.b.result);
}

/* ================================================================== */
/* 设置                                                                 */
/* ================================================================== */
typedef struct { DlgBase b; Settings s; int changed; } SettingsDlg;

static void st_load(SettingsDlg *sd) {
    Settings *s = &sd->s;
    HWND th = GetDlgItem(sd->b.hwnd, IDC_SET_THEME);
    for (int i = 0; i < theme_count(); i++) {
        Theme *t = theme_get(i);
        wchar_t *n = utf8_to_wide(t->name);
        SendMessageW(th, CB_ADDSTRING, 0, (LPARAM)n);
        free(n);
    }
    SendDlgItemMessageW(sd->b.hwnd, IDC_SET_THEME, CB_SETCURSEL, s->theme, 0);
    wset(sd->b.hwnd, IDC_SET_FONT, (wchar_t *)utf8_to_wide(s->font));
    wchar_t buf[32]; _snwprintf(buf, 32, L"%d", s->font_size); buf[31] = 0;
    wset(sd->b.hwnd, IDC_SET_FONTSIZE, buf);
    _snwprintf(buf, 32, L"%d", s->line_pad); buf[31] = 0;
    wset(sd->b.hwnd, IDC_SET_LINEPAD, buf);
    SendDlgItemMessageW(sd->b.hwnd, IDC_SET_CURSOR, CB_SETCURSEL, s->cursor_style, 0);
    if (s->cursor_blink) SendDlgItemMessageW(sd->b.hwnd, IDC_SET_BLINK, BM_SETCHECK, BST_CHECKED, 0);
    _snwprintf(buf, 32, L"%d", s->scrollback); buf[31] = 0;
    wset(sd->b.hwnd, IDC_SET_SCROLLBACK, buf);
    if (s->copy_on_select) SendDlgItemMessageW(sd->b.hwnd, IDC_SET_COPYSEL, BM_SETCHECK, BST_CHECKED, 0);
    if (s->right_click_paste) SendDlgItemMessageW(sd->b.hwnd, IDC_SET_RIGHTCLICK, BM_SETCHECK, BST_CHECKED, 0);
    if (s->confirm_paste) SendDlgItemMessageW(sd->b.hwnd, IDC_SET_CONFIRM, BM_SETCHECK, BST_CHECKED, 0);
    if (s->restore_tabs) SendDlgItemMessageW(sd->b.hwnd, IDC_SET_RESTORE, BM_SETCHECK, BST_CHECKED, 0);
    if (s->bell) SendDlgItemMessageW(sd->b.hwnd, IDC_SET_BELL, BM_SETCHECK, BST_CHECKED, 0);
    if (s->log_auto) SendDlgItemMessageW(sd->b.hwnd, IDC_SET_LOGAUTO, BM_SETCHECK, BST_CHECKED, 0);
    if (s->show_sidebar) SendDlgItemMessageW(sd->b.hwnd, IDC_SET_ALTTAB, BM_SETCHECK, BST_CHECKED, 0);
    if (s->show_toolbar) SendDlgItemMessageW(sd->b.hwnd, IDC_SET_SMOOTH, BM_SETCHECK, BST_CHECKED, 0);
    wset(sd->b.hwnd, IDC_SET_LOGDIR, s->log_dir);
    wset(sd->b.hwnd, IDC_SET_DOWNLOAD, s->download_dir);
    _snwprintf(buf, 32, L"%d", s->ssh_keepalive); buf[31] = 0;
    wset(sd->b.hwnd, IDC_SET_KEEPALIVE, buf);
    {
        wchar_t *hw = utf8_to_wide(s->highlights);
        wchar_t hbuf[640];
        int k = 0;
        for (int i = 0; hw && hw[i] && k < 630; i++) {
            wchar_t c = hw[i];
            if (c == ';') {
                if (k < 628) hbuf[k++] = '\r';
                hbuf[k++] = '\n';
                continue;
            }
            hbuf[k++] = c;
        }
        hbuf[k] = 0;
        wset(sd->b.hwnd, IDC_SET_HIGHLIGHTS, hbuf);
        free(hw);
    }
}

static void st_collect(SettingsDlg *sd) {
    Settings *s = &sd->s;
    s->theme = (int)SendDlgItemMessageW(sd->b.hwnd, IDC_SET_THEME, CB_GETCURSEL, 0, 0);
    wget(sd->b.hwnd, IDC_SET_FONT, s->font, sizeof(s->font), "Cascadia Mono");
    s->font_size = wint(sd->b.hwnd, IDC_SET_FONTSIZE, 11);
    s->line_pad = wint(sd->b.hwnd, IDC_SET_LINEPAD, 2);
    s->cursor_style = (int)SendDlgItemMessageW(sd->b.hwnd, IDC_SET_CURSOR, CB_GETCURSEL, 0, 0);
    s->cursor_blink = SendDlgItemMessageW(sd->b.hwnd, IDC_SET_BLINK, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s->scrollback = wint(sd->b.hwnd, IDC_SET_SCROLLBACK, 8000);
    s->copy_on_select = SendDlgItemMessageW(sd->b.hwnd, IDC_SET_COPYSEL, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s->right_click_paste = SendDlgItemMessageW(sd->b.hwnd, IDC_SET_RIGHTCLICK, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s->confirm_paste = SendDlgItemMessageW(sd->b.hwnd, IDC_SET_CONFIRM, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s->restore_tabs = SendDlgItemMessageW(sd->b.hwnd, IDC_SET_RESTORE, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s->bell = SendDlgItemMessageW(sd->b.hwnd, IDC_SET_BELL, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s->log_auto = SendDlgItemMessageW(sd->b.hwnd, IDC_SET_LOGAUTO, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s->show_sidebar = SendDlgItemMessageW(sd->b.hwnd, IDC_SET_ALTTAB, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s->show_toolbar = SendDlgItemMessageW(sd->b.hwnd, IDC_SET_SMOOTH, BM_GETCHECK, 0, 0) == BST_CHECKED;
    wchar_t wt[MAX_PATH * 2];
    GetDlgItemTextW(sd->b.hwnd, IDC_SET_LOGDIR, wt, MAX_PATH * 2);
    wcsncpy(s->log_dir, wt, MAX_PATH * 2 - 1);
    GetDlgItemTextW(sd->b.hwnd, IDC_SET_DOWNLOAD, wt, MAX_PATH * 2);
    wcsncpy(s->download_dir, wt, MAX_PATH * 2 - 1);
    s->ssh_keepalive = wint(sd->b.hwnd, IDC_SET_KEEPALIVE, 30);
    wget(sd->b.hwnd, IDC_SET_HIGHLIGHTS, s->highlights, sizeof(s->highlights), "");
}

static void st_browse(SettingsDlg *sd, int id) {
    wchar_t path[MAX_PATH * 2] = { 0 };
    BROWSEINFOW bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = sd->b.hwnd;
    bi.lpszTitle = L"选择目录";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST idl = SHBrowseForFolderW(&bi);
    if (idl) {
        SHGetPathFromIDListW(idl, path);
        CoTaskMemFree(idl);
        if (path[0]) wset(sd->b.hwnd, id, path);
    }
}

static void st_apply(SettingsDlg *sd) {
    st_collect(sd);
    g_app.set = sd->s;
    if (g_app.set.log_dir[0] == 0) wcscpy(g_app.set.log_dir, L"logs");
    if (g_app.set.download_dir[0] == 0) wcscpy(g_app.set.download_dir, L"downloads");
    settings_save(&g_app.set);
    g_app.show_sidebar = g_app.set.show_sidebar;
    app_apply_theme();
    app_rebuild_fonts();
    app_layout_content();
    PostMessageW(g_app.main, WM_YT_APPLY_SETTINGS, 0, 0);
    if (g_app.sidebar) sidebar_refresh(g_app.sidebar);
    sd->b.result = 1;
}

static LRESULT CALLBACK SettingsProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    SettingsDlg *sd = (SettingsDlg *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        sd = (SettingsDlg *)cs->lpCreateParams;
        sd->b.hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)sd);
        int lx = app_dpi(20), x = app_dpi(120), w = app_dpi(300), y = app_dpi(18), lh = app_dpi(34);
        mk_label(h, L"主题", lx, y + 3, app_dpi(80));
        mk_combo(h, IDC_SET_THEME, x, y, w, app_dpi(240));
        y += lh;
        mk_label(h, L"字体", lx, y + 3, app_dpi(80));
        mk_edit(h, IDC_SET_FONT, L"", x, y, w, app_dpi(24), 0);
        y += lh;
        mk_label(h, L"字号 (pt)", lx, y + 3, app_dpi(80));
        mk_edit(h, IDC_SET_FONTSIZE, L"11", x, y, app_dpi(80), app_dpi(24), ES_NUMBER);
        mk_label(h, L"行间距", x + app_dpi(96), y + 3, app_dpi(60));
        mk_edit(h, IDC_SET_LINEPAD, L"2", x + app_dpi(160), y, app_dpi(60), app_dpi(24), ES_NUMBER);
        y += lh;
        mk_label(h, L"光标", lx, y + 3, app_dpi(80));
        HWND cc = mk_combo(h, IDC_SET_CURSOR, x, y, app_dpi(160), app_dpi(240));
        SendMessageW(cc, CB_ADDSTRING, 0, (LPARAM)L"跟随程序");
        SendMessageW(cc, CB_ADDSTRING, 0, (LPARAM)L"方块");
        SendMessageW(cc, CB_ADDSTRING, 0, (LPARAM)L"下划线");
        SendMessageW(cc, CB_ADDSTRING, 0, (LPARAM)L"竖线");
        mk_check(h, IDC_SET_BLINK, L"闪烁", x + app_dpi(172), y, app_dpi(80), 1);
        y += lh;
        mk_label(h, L"回滚行数", lx, y + 3, app_dpi(80));
        mk_edit(h, IDC_SET_SCROLLBACK, L"8000", x, y, app_dpi(100), app_dpi(24), ES_NUMBER);
        y += lh;
        mk_check(h, IDC_SET_COPYSEL, L"选择即复制", lx, y, app_dpi(150), 0);
        mk_check(h, IDC_SET_RIGHTCLICK, L"右键粘贴", x + app_dpi(10), y, app_dpi(130), 0);
        y += app_dpi(28);
        mk_check(h, IDC_SET_CONFIRM, L"粘贴前确认", lx, y, app_dpi(150), 0);
        mk_check(h, IDC_SET_RESTORE, L"恢复标签", x + app_dpi(10), y, app_dpi(130), 1);
        y += app_dpi(28);
        mk_check(h, IDC_SET_BELL, L"响铃", lx, y, app_dpi(150), 1);
        mk_check(h, IDC_SET_LOGAUTO, L"自动日志", x + app_dpi(10), y, app_dpi(130), 0);
        y += app_dpi(28);
        mk_check(h, IDC_SET_ALTTAB, L"显示侧边栏", lx, y, app_dpi(150), 1);
        mk_check(h, IDC_SET_SMOOTH, L"显示工具栏", x + app_dpi(10), y, app_dpi(130), 1);
        y += app_dpi(34);
        mk_label(h, L"日志目录", lx, y + 3, app_dpi(80));
        mk_edit(h, IDC_SET_LOGDIR, L"", x, y, app_dpi(330), app_dpi(24), 0);
        mk_button(h, IDC_SET_LOGDIRB, L"…", x + app_dpi(338), y, app_dpi(40), app_dpi(24), 0);
        y += lh;
        mk_label(h, L"下载目录", lx, y + 3, app_dpi(80));
        mk_edit(h, IDC_SET_DOWNLOAD, L"", x, y, app_dpi(330), app_dpi(24), 0);
        mk_button(h, IDC_SET_DOWNLOADB, L"…", x + app_dpi(338), y, app_dpi(40), app_dpi(24), 0);
        y += lh;
        mk_label(h, L"SSH 保活", lx, y + 3, app_dpi(80));
        mk_edit(h, IDC_SET_KEEPALIVE, L"30", x, y, app_dpi(80), app_dpi(24), ES_NUMBER);
        y += lh;
        mk_label(h, L"高亮规则", lx, y + 3, app_dpi(80));
        mk_label(h, L"（每行 文本=>#RRGGBB，可加 # 注释）", x + app_dpi(2), y + 3, app_dpi(300));
        y += app_dpi(24);
        mk_edit(h, IDC_SET_HIGHLIGHTS, L"", x, y, app_dpi(378), app_dpi(66),
                ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL);
        y += app_dpi(76);
        mk_button(h, IDC_SET_OK, L"确定", app_dpi(430), y, app_dpi(90), app_dpi(30), 1);
        mk_button(h, IDC_SET_CANCEL, L"取消", app_dpi(526), y, app_dpi(80), app_dpi(30), 0);

        sd->b.bg = CreateSolidBrush(app_ui_bg());
        sd->b.editbg = CreateSolidBrush(app_ui_panel());
        st_load(sd);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDC_SET_OK) { st_apply(sd); DestroyWindow(h); return 0; }
        if (id == IDC_SET_CANCEL) { DestroyWindow(h); return 0; }
        if (id == IDC_SET_LOGDIRB) st_browse(sd, IDC_SET_LOGDIR);
        if (id == IDC_SET_DOWNLOADB) st_browse(sd, IDC_SET_DOWNLOAD);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        app_fill((HDC)ps.hdc, &rc, app_ui_bg());
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, app_ui_text());
        SetBkColor(dc, app_ui_bg());
        return (LRESULT)sd->b.bg;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, app_ui_text());
        SetBkColor(dc, app_ui_panel());
        return (LRESULT)sd->b.editbg;
    }
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        if (sd->b.bg) DeleteObject(sd->b.bg);
        if (sd->b.editbg) DeleteObject(sd->b.editbg);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void app_show_settings(HWND parent) {
    register_dlg(L"YSettingsDlg", SettingsProc);
    SettingsDlg sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.s = g_app.set;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"YSettingsDlg", L"YTerminal 设置",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                               CW_USEDEFAULT, CW_USEDEFAULT, app_dpi(640), app_dpi(620),
                               parent, NULL, g_app.inst, &sd);
    if (dlg) modal_run(dlg, &sd.b.result);
}

/* ================================================================== */
/* 快速连接                                                             */
/* ================================================================== */
typedef struct { DlgBase b; Session s; char host[256], user[96], pass[256]; int port, proto, opened; } QuickDlg;

static LRESULT CALLBACK QuickProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    QuickDlg *qd = (QuickDlg *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        qd = (QuickDlg *)cs->lpCreateParams;
        qd->b.hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)qd);
        int lx = app_dpi(18), x = app_dpi(96), w = app_dpi(300), y = app_dpi(16), lh = app_dpi(34);
        mk_label(h, L"协议", lx, y + 3, app_dpi(70));
        HWND pc = mk_combo(h, IDC_DLG_PROTO, x, y, app_dpi(120), app_dpi(200));
        SendMessageW(pc, CB_ADDSTRING, 0, (LPARAM)L"SSH");
        SendMessageW(pc, CB_ADDSTRING, 0, (LPARAM)L"Telnet");
        SendMessageW(pc, CB_ADDSTRING, 0, (LPARAM)L"串口");
        SendMessageW(pc, CB_ADDSTRING, 0, (LPARAM)L"本地 Shell");
        SendMessageW(pc, CB_SETCURSEL, 0, 0);
        y += lh;
        mk_label(h, L"主机", lx, y + 3, app_dpi(70));
        mk_edit(h, IDC_DLG_HOST, L"", x, y, app_dpi(216), app_dpi(24), 0);
        mk_label(h, L"端口", x + app_dpi(224), y + 3, app_dpi(36));
        mk_edit(h, IDC_DLG_PORT, L"22", x + app_dpi(260), y, app_dpi(40), app_dpi(24), ES_NUMBER);
        y += lh;
        mk_label(h, L"用户名", lx, y + 3, app_dpi(70));
        mk_edit(h, IDC_DLG_USER, L"", x, y, w, app_dpi(24), 0);
        y += lh;
        mk_label(h, L"密码", lx, y + 3, app_dpi(70));
        mk_edit(h, IDC_DLG_PASS, L"", x, y, w, app_dpi(24), ES_PASSWORD);
        y += lh;
        mk_edit(h, IDC_DLG_EXTRA, L"", x, y, w, app_dpi(24), 0);
        mk_label(h, L"名称", lx, y + 3, app_dpi(70));
        y += app_dpi(36);
        mk_check(h, IDC_DLG_FAVORITE, L"保存到会话列表", lx, y, app_dpi(200), 1);
        mk_button(h, IDC_DLG_OK, L"连接", app_dpi(300), app_dpi(210), app_dpi(90), app_dpi(30), 1);
        mk_button(h, IDC_DLG_CANCEL, L"取消", app_dpi(396), app_dpi(210), app_dpi(80), app_dpi(30), 0);
        qd->b.bg = CreateSolidBrush(app_ui_bg());
        qd->b.editbg = CreateSolidBrush(app_ui_panel());
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDC_DLG_CANCEL) { DestroyWindow(h); return 0; }
        if (id == IDC_DLG_OK) {
            int proto = (int)SendDlgItemMessageW(h, IDC_DLG_PROTO, CB_GETCURSEL, 0, 0);
            proto = proto == 0 ? PROTO_SSH : proto == 1 ? PROTO_TELNET : proto == 2 ? PROTO_SERIAL : PROTO_LOCAL;
            Session s = session_defaults(proto);
            wget(h, IDC_DLG_HOST, s.host, sizeof(s.host), "");
            s.port = wint(h, IDC_DLG_PORT, proto == PROTO_TELNET ? 23 : 22);
            wget(h, IDC_DLG_USER, s.user, sizeof(s.user), "");
            wget(h, IDC_DLG_PASS, s.password, sizeof(s.password), "");
            if (proto == PROTO_SERIAL) { wget(h, IDC_DLG_HOST, s.port_name, sizeof(s.port_name), "COM1"); }
            if (s.name[0] == 0) {
                if (proto == PROTO_LOCAL) strcpy(s.name, "本地 Shell");
                else if (proto == PROTO_SERIAL) strcpy(s.name, "串口会话");
                else _snprintf(s.name, sizeof(s.name), "%s@%s", s.user, s.host);
            }
            if (SendDlgItemMessageW(h, IDC_DLG_FAVORITE, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                sessions_add(&g_app.sessions, &s);
                sessions_save(&g_app.sessions);
                PostMessageW(g_app.main, WM_YT_SIDEBAR_REFRESH, 0, 0);
            }
            qd->opened = 1;
            qd->b.result = 1;
            app_open_session(&s, 1);
            DestroyWindow(h);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        app_fill(ps.hdc, &rc, app_ui_bg());
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp; SetTextColor(dc, app_ui_text()); SetBkColor(dc, app_ui_bg());
        return (LRESULT)qd->b.bg;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp; SetTextColor(dc, app_ui_text()); SetBkColor(dc, app_ui_panel());
        return (LRESULT)qd->b.editbg;
    }
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        if (qd->b.bg) DeleteObject(qd->b.bg);
        if (qd->b.editbg) DeleteObject(qd->b.editbg);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void app_show_quick_connect(HWND parent) {
    register_dlg(L"YQuickDlg", QuickProc);
    QuickDlg qd;
    ZeroMemory(&qd, sizeof(qd));
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"YQuickDlg", L"快速连接",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               CW_USEDEFAULT, CW_USEDEFAULT, app_dpi(500), app_dpi(290),
                               parent, NULL, g_app.inst, &qd);
    if (dlg) modal_run(dlg, &qd.b.result);
}

/* ================================================================== */
/* 关于 / 快捷键                                                        */
/* ================================================================== */
static LRESULT CALLBACK AboutProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        mk_button(h, IDC_DLG_OK, L"关闭", app_dpi(400), app_dpi(300), app_dpi(90), app_dpi(30), 1);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_DLG_OK) DestroyWindow(h);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        app_fill(ps.hdc, &rc, app_ui_bg());
        RECT t = { app_dpi(30), app_dpi(24), rc.right - app_dpi(30), app_dpi(56) };
        app_draw_text(ps.hdc, L"YTerminal", &t, g_app.f_ui_b, app_ui_accent(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT v = { app_dpi(30), app_dpi(58), rc.right - app_dpi(30), app_dpi(80) };
        app_draw_text(ps.hdc, L"纯 C / Win32 / GDI / ConPTY  ·  零运行时依赖  ·  v1.0", &v, g_app.f_ui, app_ui_dim(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT d = { app_dpi(30), app_dpi(96), rc.right - app_dpi(30), app_dpi(220) };
        app_draw_text(ps.hdc,
            L"一款对标 WindTerm / SecureCRT / MobaXterm 的 Windows 终端：\n"
            L"· SSH / Telnet / 串口 / 本地 Shell（ConPTY）\n"
            L"· 标签 + 二分屏、输入广播、会话管理器\n"
            L"· 完整 VT100/xterm 模拟、256 色/真彩、宽字符、鼠标上报\n"
            L"· 会话日志、触发器、快捷命令、SecureCRT/MobaXterm/PuTTY 导入\n"
            L"· DPAPI 凭据加密、便携模式\n\n"
            L"参考开源项目：Windows Terminal / WezTerm / Alacritty / Tabby",
            &d, g_app.f_ui, app_ui_text(), DT_LEFT | DT_WORDBREAK);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: DestroyWindow(h); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void app_show_about(HWND parent) {
    register_dlg(L"YAboutDlg", AboutProc);
    int r = 0;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"YAboutDlg", L"关于 YTerminal",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               CW_USEDEFAULT, CW_USEDEFAULT, app_dpi(520), app_dpi(360),
                               parent, NULL, g_app.inst, NULL);
    if (dlg) modal_run(dlg, &r);
}

/* ================================================================== */
/* 重命名标签                                                           */
/* ================================================================== */
typedef struct { DlgBase b; wchar_t title[192]; } RenameDlg;

static LRESULT CALLBACK RenameProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    RenameDlg *rd = (RenameDlg *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        rd = (RenameDlg *)cs->lpCreateParams;
        rd->b.hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)rd);
        mk_edit(h, IDC_DLG_NAME, rd->title, app_dpi(18), app_dpi(18), app_dpi(330), app_dpi(26), 0);
        mk_button(h, IDC_DLG_OK, L"确定", app_dpi(180), app_dpi(60), app_dpi(80), app_dpi(28), 1);
        mk_button(h, IDC_DLG_CANCEL, L"取消", app_dpi(270), app_dpi(60), app_dpi(80), app_dpi(28), 0);
        SetFocus(GetDlgItem(h, IDC_DLG_NAME));
        SendDlgItemMessageW(h, IDC_DLG_NAME, EM_SETSEL, 0, -1);
        rd->b.bg = CreateSolidBrush(app_ui_bg());
        rd->b.editbg = CreateSolidBrush(app_ui_panel());
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDC_DLG_OK) {
            GetDlgItemTextW(h, IDC_DLG_NAME, rd->title, 192);
            HWND v = app_active_view();
            if (v) term_view_set_title(v, rd->title);
            PostMessageW(g_app.main, WM_YT_TITLE_CHANGED, (WPARAM)v, 0);
            rd->b.result = 1;
            DestroyWindow(h);
        } else if (id == IDC_DLG_CANCEL) DestroyWindow(h);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        app_fill(ps.hdc, &rc, app_ui_bg());
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp; SetTextColor(dc, app_ui_text()); SetBkColor(dc, app_ui_bg());
        return (LRESULT)rd->b.bg;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp; SetTextColor(dc, app_ui_text()); SetBkColor(dc, app_ui_panel());
        return (LRESULT)rd->b.editbg;
    }
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        if (rd->b.bg) DeleteObject(rd->b.bg);
        if (rd->b.editbg) DeleteObject(rd->b.editbg);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void app_rename_active_tab(HWND parent) {
    register_dlg(L"YRenameDlg", RenameProc);
    RenameDlg rd;
    ZeroMemory(&rd, sizeof(rd));
    HWND v = app_active_view();
    const wchar_t *cur = v ? term_view_title(v) : L"";
    wcsncpy(rd.title, cur, 192);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"YRenameDlg", L"重命名标签",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               CW_USEDEFAULT, CW_USEDEFAULT, app_dpi(380), app_dpi(130),
                               parent, NULL, g_app.inst, &rd);
    if (dlg) modal_run(dlg, &rd.b.result);
}

/* ================================================================== */
/* 快捷命令                                                             */
/* ================================================================== */
typedef struct { DlgBase b; Session *s; char cmds[1024]; char trig[512]; } CmdDlg;

static LRESULT CALLBACK CmdProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    CmdDlg *cd = (CmdDlg *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        cd = (CmdDlg *)cs->lpCreateParams;
        cd->b.hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cd);
        mk_label(h, L"快捷命令（每行：命令|说明，Ctrl+K 弹出菜单）", app_dpi(16), app_dpi(12), app_dpi(400));
        mk_edit(h, IDC_DLG_QUICKCMDS, (wchar_t *)utf8_to_wide(cd->cmds), app_dpi(16), app_dpi(34), app_dpi(448), app_dpi(150),
                ES_MULTILINE | WS_VSCROLL | ES_WANTRETURN);
        mk_label(h, L"触发器（每行：匹配模式=>发送内容，如 \"Password:\"=>\"myPass\\r\"）", app_dpi(16), app_dpi(196), app_dpi(440));
        mk_edit(h, IDC_DLG_TRIGGERS, (wchar_t *)utf8_to_wide(cd->trig), app_dpi(16), app_dpi(218), app_dpi(448), app_dpi(90),
                ES_MULTILINE | WS_VSCROLL | ES_WANTRETURN);
        mk_button(h, IDC_DLG_OK, L"保存", app_dpi(300), app_dpi(322), app_dpi(80), app_dpi(30), 1);
        mk_button(h, IDC_DLG_CANCEL, L"取消", app_dpi(388), app_dpi(322), app_dpi(80), app_dpi(30), 0);
        cd->b.bg = CreateSolidBrush(app_ui_bg());
        cd->b.editbg = CreateSolidBrush(app_ui_panel());
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDC_DLG_OK) {
            wget(h, IDC_DLG_QUICKCMDS, cd->cmds, sizeof(cd->cmds), "");
            wget(h, IDC_DLG_TRIGGERS, cd->trig, sizeof(cd->trig), "");
            if (cd->s) {
                strncpy(cd->s->quick_cmds, cd->cmds, sizeof(cd->s->quick_cmds) - 1);
                strncpy(cd->s->triggers, cd->trig, sizeof(cd->s->triggers) - 1);
                int idx = sessions_find_index(&g_app.sessions, cd->s->id);
                if (idx >= 0) {
                    strncpy(g_app.sessions.items[idx].quick_cmds, cd->cmds, sizeof(cd->s->quick_cmds) - 1);
                    strncpy(g_app.sessions.items[idx].triggers, cd->trig, sizeof(cd->s->triggers) - 1);
                    sessions_save(&g_app.sessions);
                }
            }
            cd->b.result = 1;
            DestroyWindow(h);
        } else if (id == IDC_DLG_CANCEL) DestroyWindow(h);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        app_fill(ps.hdc, &rc, app_ui_bg());
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp; SetTextColor(dc, app_ui_text()); SetBkColor(dc, app_ui_bg());
        return (LRESULT)cd->b.bg;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp; SetTextColor(dc, app_ui_text()); SetBkColor(dc, app_ui_panel());
        return (LRESULT)cd->b.editbg;
    }
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        if (cd->b.bg) DeleteObject(cd->b.bg);
        if (cd->b.editbg) DeleteObject(cd->b.editbg);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void app_quick_commands(HWND parent) {
    register_dlg(L"YCmdDlg", CmdProc);
    CmdDlg cd;
    ZeroMemory(&cd, sizeof(cd));
    HWND v = app_active_view();
    cd.s = v ? term_view_session(v) : NULL;
    if (cd.s) {
        strncpy(cd.cmds, cd.s->quick_cmds, sizeof(cd.cmds) - 1);
        strncpy(cd.trig, cd.s->triggers, sizeof(cd.trig) - 1);
    }
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"YCmdDlg", L"快捷命令与触发器",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               CW_USEDEFAULT, CW_USEDEFAULT, app_dpi(490), app_dpi(380),
                               parent, NULL, g_app.inst, &cd);
    if (dlg) modal_run(dlg, &cd.b.result);
}

/* ================================================================== */
/* 快捷键一览                                                           */
/* ================================================================== */
typedef struct { const wchar_t *key; const wchar_t *desc; } ScRow;
static const ScRow g_sc[] = {
    { L"Ctrl+T",        L"新建 SSH 会话" },
    { L"Ctrl+Shift+T",  L"新建本地终端" },
    { L"Ctrl+Tab",      L"新建标签" },
    { L"Ctrl+W",        L"关闭标签" },
    { L"Ctrl+Shift+C",  L"复制选区" },
    { L"Ctrl+Shift+V",  L"粘贴" },
    { L"Ctrl+Shift+F",  L"查找文本" },
    { L"Esc",           L"关闭查找" },
    { L"Ctrl+=",        L"放大字号" },
    { L"Ctrl+-",        L"缩小字号" },
    { L"Ctrl+0",        L"重置字号" },
    { L"Ctrl+Shift+D",  L"上下分屏" },
    { L"Ctrl+Shift+E",  L"左右分屏" },
    { L"Ctrl+Shift+P",  L"命令面板" },
    { L"Ctrl+Shift+K",  L"快捷命令与规则" },
    { L"Ctrl+Shift+S",  L"SFTP 传输" },
    { L"Ctrl+Shift+L",  L"日志记录开关" },
    { L"Alt+字母",      L"发送 ESC 序列" },
};
#define SC_COUNT ((int)(sizeof(g_sc) / sizeof(g_sc[0])))
#define SC_COLS 2
#define SC_KEY_BASE 6100
#define SC_DESC_BASE 6200

static HBRUSH g_sc_bg = NULL;

static void sc_make_label(HWND h, int id, const wchar_t *text, int x, int y, int w, int style) {
    HWND l = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | style,
                             x, y, w, app_dpi(24), h, (HMENU)(INT_PTR)id, g_app.inst, NULL);
    SendMessageW(l, WM_SETFONT, (WPARAM)g_app.f_ui, TRUE);
}

static LRESULT CALLBACK ScProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        int rows = (SC_COUNT + SC_COLS - 1) / SC_COLS;
        int colw = app_dpi(300), keyw = app_dpi(126);
        for (int i = 0; i < SC_COUNT; i++) {
            int col = i / rows, row = i % rows;
            int x = app_dpi(24) + col * colw;
            int y = app_dpi(72) + row * app_dpi(30);
            sc_make_label(h, SC_KEY_BASE + i, g_sc[i].key, x, y, keyw, SS_RIGHT);
            sc_make_label(h, SC_DESC_BASE + i, g_sc[i].desc, x + keyw + app_dpi(8), y, colw - keyw - app_dpi(18), SS_LEFT);
        }
        mk_button(h, IDC_DLG_OK, L"关闭", app_dpi(516), app_dpi(360), app_dpi(96), app_dpi(32), 1);
        g_sc_bg = CreateSolidBrush(app_ui_bg());
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_DLG_OK) DestroyWindow(h);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) DestroyWindow(h);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        app_fill(ps.hdc, &rc, app_ui_bg());
        RECT hd = { 0, 0, rc.right, app_dpi(52) };
        app_fill(ps.hdc, &hd, app_ui_panel());
        RECT bar = { 0, 0, rc.right, app_dpi(2) };
        app_fill(ps.hdc, &bar, app_ui_accent());
        RECT tr = { app_dpi(24), app_dpi(10), rc.right - app_dpi(24), app_dpi(42) };
        app_draw_text(ps.hdc, L"键盘快捷键", &tr, g_app.f_ui, app_ui_accent(),
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT fr = { app_dpi(24), rc.bottom - app_dpi(44), rc.right - app_dpi(140), rc.bottom - app_dpi(14) };
        app_draw_text(ps.hdc, L"提示：标签上右键可关闭其他/右侧标签、复制标题", &fr, g_app.f_ui, app_ui_dim(),
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        int id = GetDlgCtrlID((HWND)lp);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, (id >= SC_KEY_BASE && id < SC_KEY_BASE + SC_COUNT) ? app_ui_dim() : app_ui_text());
        return (LRESULT)g_sc_bg;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        if (g_sc_bg) { DeleteObject(g_sc_bg); g_sc_bg = NULL; }
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void app_show_shortcuts(HWND parent) {
    register_dlg(L"YScDlg", ScProc);
    int result = 0;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"YScDlg", L"快捷键一览",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               CW_USEDEFAULT, CW_USEDEFAULT, app_dpi(640), app_dpi(450),
                               parent, NULL, g_app.inst, NULL);
    if (dlg) modal_run(dlg, &result);
}
/* ================================================================== */
/* 端口转发管理器                                                       */
/* ================================================================== */
typedef struct { DlgBase b; Session *s; char text[512]; } TunDlg;

static LRESULT CALLBACK TunProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    TunDlg *td = (TunDlg *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        td = (TunDlg *)cs->lpCreateParams;
        td->b.hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)td);
        mk_label(h, L"端口转发规则（每行一条，留空即无）", app_dpi(18), app_dpi(14), app_dpi(400));
        mk_label(h, L"L:8080:localhost:80  ·  R:9000:db:5432  ·  D:1080", app_dpi(18), app_dpi(38), app_dpi(440));
        wchar_t wtext[600];
        int k = 0;
        for (int i = 0; td->text[i] && k < 590; i++) {
            char c = td->text[i];
            if (c == ';' || c == ',') {
                wtext[k++] = L'\r';
                wtext[k++] = L'\n';
            } else {
                wtext[k++] = (wchar_t)c;
            }
        }
        wtext[k] = 0;
        mk_edit(h, IDC_DLG_TUNNELS, wtext, app_dpi(18), app_dpi(64), app_dpi(452), app_dpi(210),
                ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL);
        mk_button(h, IDC_DLG_OK, L"保存", app_dpi(290), app_dpi(288), app_dpi(84), app_dpi(30), 1);
        mk_button(h, IDC_DLG_CANCEL, L"取消", app_dpi(384), app_dpi(288), app_dpi(84), app_dpi(30), 0);
        td->b.bg = CreateSolidBrush(app_ui_bg());
        td->b.editbg = CreateSolidBrush(app_ui_panel());
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDC_DLG_OK) {
            wchar_t wt[600];
            GetDlgItemTextW(h, IDC_DLG_TUNNELS, wt, 590);
            char raw[1024];
            int n = 0;
            for (int i = 0; wt[i] && n < 1000; i++) {
                char c = (wt[i] < 0x80) ? (char)wt[i] : ' ';
                if (c == '\r') continue;
                if (c == '\n') c = ';';
                raw[n++] = c;
            }
            raw[n] = 0;

            char out[512];
            int used = 0;
            out[0] = 0;
            char buf[1024];
            strncpy(buf, raw, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = 0;
            char *p = buf;
            int bad_line = 0, line_no = 0;
            while (*p) {
                line_no++;
                char *e = p;
                while (*e && *e != ';') e++;
                char seg[160];
                int len = (int)(e - p);
                if (len > 150) len = 150;
                memcpy(seg, p, (size_t)len);
                seg[len] = 0;
                p = (*e) ? e + 1 : e;
                while (*p == ';' || *p == ' ' || *p == '\t') p++;
                char t[160];
                int s0 = 0, t0 = 0;
                while (seg[s0] == ' ' || seg[s0] == '\t') s0++;
                while (seg[s0]) t[t0++] = seg[s0++];
                while (t0 > 0 && (t[t0 - 1] == ' ' || t[t0 - 1] == '\t')) t0--;
                t[t0] = 0;
                if (!t[0]) continue;
                YtFwd f;
                if (!yt_fwd_parse(t, &f)) { bad_line = line_no; break; }
                char fb[128];
                yt_fwd_format(&f, fb, sizeof(fb));
                int fl = (int)strlen(fb);
                if (used + fl + 2 < (int)sizeof(out)) {
                    if (used) out[used++] = ';';
                    memcpy(out + used, fb, (size_t)fl);
                    used += fl;
                    out[used] = 0;
                }
            }
            if (bad_line) {
                wchar_t msg[160];
                _snwprintf(msg, 160, L"第 %d 行格式不正确。\n示例：L:8080:localhost:80 或 D:1080", bad_line);
                msg[159] = 0;
                MessageBoxW(h, msg, L"端口转发", MB_ICONWARNING);
                return 0;
            }
            if (td->s) {
                strncpy(td->s->tunnels, out, sizeof(td->s->tunnels) - 1);
                td->s->tunnels[sizeof(td->s->tunnels) - 1] = 0;
                int idx = sessions_find_index(&g_app.sessions, td->s->id);
                if (idx >= 0) {
                    strncpy(g_app.sessions.items[idx].tunnels, out, sizeof(td->s->tunnels) - 1);
                    sessions_save(&g_app.sessions);
                }
                app_status_set(L"端口转发已保存，重连后生效");
            }
            td->b.result = 1;
            DestroyWindow(h);
        } else if (id == IDC_DLG_CANCEL) DestroyWindow(h);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        app_fill(ps.hdc, &rc, app_ui_bg());
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp; SetTextColor(dc, app_ui_text()); SetBkColor(dc, app_ui_bg());
        return (LRESULT)td->b.bg;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp; SetTextColor(dc, app_ui_text()); SetBkColor(dc, app_ui_panel());
        return (LRESULT)td->b.editbg;
    }
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        if (td->b.bg) DeleteObject(td->b.bg);
        if (td->b.editbg) DeleteObject(td->b.editbg);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void app_show_tunnels(HWND parent) {
    register_dlg(L"YTunDlg", TunProc);
    TunDlg td;
    ZeroMemory(&td, sizeof(td));
    HWND v = app_active_view();
    td.s = v ? term_view_session(v) : NULL;
    if (td.s) strncpy(td.text, td.s->tunnels, sizeof(td.text) - 1);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"YTunDlg", L"端口转发",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               CW_USEDEFAULT, CW_USEDEFAULT, app_dpi(500), app_dpi(380),
                               parent, NULL, g_app.inst, &td);
    if (dlg) modal_run(dlg, &td.b.result);
}