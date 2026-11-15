#include "yt.h"
#include "win_ui_bridge.h"

#define ID_TAB        1001
#define ID_TERM_BASE  2000
#define ID_STATUS     1002
#define ID_BTN_CONNECT 1010
#define ID_BTN_CLOSE   1014
#define ID_CMB_KIND    1020
#define ID_EDIT_HOST   1021
#define ID_EDIT_PORT   1022
#define ID_EDIT_USER   1023
#define ID_EDIT_PASS   1024
#define ID_EDIT_COM    1025
#define ID_EDIT_BAUD   1026

#define IDM_NEW_LOCAL  4001
#define IDM_NEW_SSH    4002
#define IDM_NEW_SER    4003
#define IDM_CLOSE_TAB  4004
#define IDM_SAVE_SESS  4005
#define IDM_LOAD_SESS  4006
#define IDM_LOG_TOGGLE 4007
#define IDM_EXIT       4010

typedef struct {
    HWND hwnd, tab, status;
    HWND cmb_kind, edit_host, edit_port, edit_user, edit_pass, edit_com, edit_baud;
    HWND btn_connect, btn_close;
    HWND lbl_host, lbl_port, lbl_user, lbl_pass, lbl_com, lbl_baud;
    HINSTANCE hi;
    int ntabs;
    HWND terms[YT_MAX_TABS];
    YtSession *sess[YT_MAX_TABS];
    wchar_t exe_dir[YT_PATH_MAX];
    wchar_t sess_path[YT_PATH_MAX];
} App;

static App g;

static void set_status(const wchar_t *s) { SetWindowTextW(g.status, s); }
static int cur_tab(void) { return (int)SendMessageW(g.tab, TCM_GETCURSEL, 0, 0); }

static int kind_sel(void)
{
    return (int)SendMessageW(g.cmb_kind, CB_GETCURSEL, 0, 0);
}

static void sync_kind_ui(void)
{
    int k = kind_sel(); /* 0 local 1 ssh 2 serial */
    int ssh = (k == 1), ser = (k == 2);
    ShowWindow(g.lbl_host, ssh ? SW_SHOW : SW_HIDE);
    ShowWindow(g.edit_host, ssh ? SW_SHOW : SW_HIDE);
    ShowWindow(g.lbl_port, ssh ? SW_SHOW : SW_HIDE);
    ShowWindow(g.edit_port, ssh ? SW_SHOW : SW_HIDE);
    ShowWindow(g.lbl_user, ssh ? SW_SHOW : SW_HIDE);
    ShowWindow(g.edit_user, ssh ? SW_SHOW : SW_HIDE);
    ShowWindow(g.lbl_pass, ssh ? SW_SHOW : SW_HIDE);
    ShowWindow(g.edit_pass, ssh ? SW_SHOW : SW_HIDE);
    ShowWindow(g.lbl_com, ser ? SW_SHOW : SW_HIDE);
    ShowWindow(g.edit_com, ser ? SW_SHOW : SW_HIDE);
    ShowWindow(g.lbl_baud, ser ? SW_SHOW : SW_HIDE);
    ShowWindow(g.edit_baud, ser ? SW_SHOW : SW_HIDE);
}

static void layout(HWND hwnd)
{
    RECT rc; int i, cur, bar = 64, tab_h = 28, st_h = 22;
    GetClientRect(hwnd, &rc);
    MoveWindow(g.cmb_kind, 8, 8, 90, 200, TRUE);
    MoveWindow(g.lbl_host, 110, 10, 36, 20, TRUE);
    MoveWindow(g.edit_host, 148, 8, 140, 24, TRUE);
    MoveWindow(g.lbl_port, 294, 10, 36, 20, TRUE);
    MoveWindow(g.edit_port, 330, 8, 56, 24, TRUE);
    MoveWindow(g.lbl_user, 394, 10, 36, 20, TRUE);
    MoveWindow(g.edit_user, 430, 8, 80, 24, TRUE);
    MoveWindow(g.lbl_pass, 518, 10, 36, 20, TRUE);
    MoveWindow(g.edit_pass, 554, 8, 90, 24, TRUE);
    MoveWindow(g.lbl_com, 110, 10, 36, 20, TRUE);
    MoveWindow(g.edit_com, 148, 8, 80, 24, TRUE);
    MoveWindow(g.lbl_baud, 236, 10, 40, 20, TRUE);
    MoveWindow(g.edit_baud, 278, 8, 90, 24, TRUE);
    MoveWindow(g.btn_connect, 660, 6, 80, 28, TRUE);
    MoveWindow(g.btn_close, 748, 6, 70, 28, TRUE);
    MoveWindow(g.tab, 0, bar, rc.right, tab_h, TRUE);
    MoveWindow(g.status, 0, rc.bottom - st_h, rc.right, st_h, TRUE);
    cur = cur_tab();
    for (i = 0; i < g.ntabs; i++) {
        ShowWindow(g.terms[i], i == cur ? SW_SHOW : SW_HIDE);
        MoveWindow(g.terms[i], 0, bar + tab_h, rc.right, rc.bottom - bar - tab_h - st_h, TRUE);
    }
    sync_kind_ui();
    if (cur >= 0 && cur < g.ntabs) term_set_focus(g.terms[cur]);
}

static void refresh_tab_title(int i)
{
    TCITEMW ti = {0};
    ti.mask = TCIF_TEXT;
    ti.pszText = g.sess[i]->title;
    SendMessageW(g.tab, TCM_SETITEMW, i, (LPARAM)&ti);
}

static int add_tab(YtSession *s)
{
    TCITEMW ti = {0};
    int idx;
    HWND term;
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
    ti.pszText = s->title[0] ? s->title : L"New";
    SendMessageW(g.tab, TCM_INSERTITEMW, idx, (LPARAM)&ti);
    g.ntabs++;
    SendMessageW(g.tab, TCM_SETCURSEL, idx, 0);
    layout(g.hwnd);
    ui_bridge_log("tab add idx=%d kind=%d", idx, (int)s->kind);
    return idx;
}

static void close_tab(int idx)
{
    int i;
    if (idx < 0 || idx >= g.ntabs) return;
    yt_session_free(g.sess[idx]);
    free(g.sess[idx]);
    DestroyWindow(g.terms[idx]);
    SendMessageW(g.tab, TCM_DELETEITEM, idx, 0);
    for (i = idx; i < g.ntabs - 1; i++) {
        g.terms[i] = g.terms[i + 1];
        g.sess[i] = g.sess[i + 1];
        g.sess[i]->tab_index = i;
    }
    g.ntabs--;
    if (g.ntabs > 0)
        SendMessageW(g.tab, TCM_SETCURSEL, idx < g.ntabs ? idx : g.ntabs - 1, 0);
    layout(g.hwnd);
    ui_bridge_log("tab close left=%d", g.ntabs);
}

static void start_ok_or_err(YtSession *s, BOOL ok)
{
    if (!ok) {
        wchar_t w[300];
        MultiByteToWideChar(CP_UTF8, 0, s->err, -1, w, 300);
        set_status(w);
        ui_bridge_log("connect fail: %s", s->err);
        MessageBoxW(g.hwnd, w, L"YTerminal", MB_ICONERROR);
        yt_session_free(s);
        free(s);
        return;
    }
    add_tab(s);
    refresh_tab_title(g.ntabs - 1);
    set_status(L"Connected");
}

static void do_connect(void)
{
    YtSession *s = (YtSession *)calloc(1, sizeof(YtSession));
    int k = kind_sel();
    yt_session_init(s);
    if (k == 0) {
        wcscpy(s->shell, L"powershell");
        start_ok_or_err(s, yt_session_start_local(s, s->shell));
    } else if (k == 1) {
        GetWindowTextW(g.edit_host, s->host, 128);
        { wchar_t b[16]; GetWindowTextW(g.edit_port, b, 16); s->port = _wtoi(b); }
        GetWindowTextW(g.edit_user, s->user, 64);
        GetWindowTextW(g.edit_pass, s->pass, 128);
        start_ok_or_err(s, yt_session_start_ssh(s));
    } else {
        GetWindowTextW(g.edit_com, s->com, 32);
        { wchar_t b[16]; GetWindowTextW(g.edit_baud, b, 16); s->baud = _wtoi(b); }
        start_ok_or_err(s, yt_session_start_serial(s));
    }
}

static void toggle_log(void)
{
    int i = cur_tab();
    wchar_t dir[YT_PATH_MAX], path[YT_PATH_MAX];
    char up[YT_PATH_MAX];
    if (i < 0 || i >= g.ntabs) return;
    if (g.sess[i]->log_fp) {
        fclose(g.sess[i]->log_fp);
        g.sess[i]->log_fp = NULL;
        g.sess[i]->log_on = FALSE;
        set_status(L"Log off");
        return;
    }
    swprintf(dir, YT_PATH_MAX, L"%ls\\logs", g.exe_dir);
    CreateDirectoryW(dir, NULL);
    swprintf(path, YT_PATH_MAX, L"%ls\\tab%d.log", dir, i);
    WideCharToMultiByte(CP_UTF8, 0, path, -1, up, sizeof(up), NULL, NULL);
    g.sess[i]->log_fp = fopen(up, "ab");
    g.sess[i]->log_on = g.sess[i]->log_fp != NULL;
    set_status(g.sess[i]->log_on ? L"Log on" : L"Log open failed");
}

static void on_state(char *out, int cap, void *user)
{
    int cur = cur_tab();
    YtSession *s = (cur >= 0 && cur < g.ntabs) ? g.sess[cur] : NULL;
    char title[128] = "";
    (void)user;
    if (s) WideCharToMultiByte(CP_UTF8, 0, s->title, -1, title, sizeof(title), NULL, NULL);
    snprintf(out, (size_t)cap,
             "{\"tabs\":%d,\"cur\":%d,\"kind\":%d,\"state\":%d,\"title\":\"%s\",\"log\":%s,\"ui_kind\":%d}",
             g.ntabs, cur,
             s ? (int)s->kind : -1,
             s ? (int)s->state : -1,
             title,
             (s && s->log_on) ? "true" : "false",
             kind_sel());
}

static HWND mk_edit(HWND p, int id, const wchar_t *text, DWORD ex)
{
    return CreateWindowExW(0, L"EDIT", text,
                           WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ex,
                           0, 0, 80, 24, p, (HMENU)(INT_PTR)id, g.hi, 0);
}

static HWND mk_lbl(HWND p, const wchar_t *t)
{
    return CreateWindowW(L"STATIC", t, WS_CHILD | WS_VISIBLE, 0, 0, 40, 20, p, 0, g.hi, 0);
}

static void create_ui(HWND hwnd)
{
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HWND all[20];
    int n = 0, i;

    g.cmb_kind = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                               0, 0, 90, 200, hwnd, (HMENU)ID_CMB_KIND, g.hi, 0);
    SendMessageW(g.cmb_kind, CB_ADDSTRING, 0, (LPARAM)L"Local");
    SendMessageW(g.cmb_kind, CB_ADDSTRING, 0, (LPARAM)L"SSH");
    SendMessageW(g.cmb_kind, CB_ADDSTRING, 0, (LPARAM)L"Serial");
    SendMessageW(g.cmb_kind, CB_SETCURSEL, 0, 0);

    g.lbl_host = mk_lbl(hwnd, L"Host");
    g.edit_host = mk_edit(hwnd, ID_EDIT_HOST, L"sshbridge.online", 0);
    g.lbl_port = mk_lbl(hwnd, L"Port");
    g.edit_port = mk_edit(hwnd, ID_EDIT_PORT, L"52222", 0);
    g.lbl_user = mk_lbl(hwnd, L"User");
    g.edit_user = mk_edit(hwnd, ID_EDIT_USER, L"demo", 0);
    g.lbl_pass = mk_lbl(hwnd, L"Pass");
    g.edit_pass = mk_edit(hwnd, ID_EDIT_PASS, L"123456", ES_PASSWORD);
    g.lbl_com = mk_lbl(hwnd, L"COM");
    g.edit_com = mk_edit(hwnd, ID_EDIT_COM, L"COM3", 0);
    g.lbl_baud = mk_lbl(hwnd, L"Baud");
    g.edit_baud = mk_edit(hwnd, ID_EDIT_BAUD, L"1500000", 0);

    g.btn_connect = CreateWindowW(L"BUTTON", L"Connect", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                  0, 0, 80, 28, hwnd, (HMENU)ID_BTN_CONNECT, g.hi, 0);
    g.btn_close = CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE,
                                0, 0, 70, 28, hwnd, (HMENU)ID_BTN_CLOSE, g.hi, 0);
    g.tab = CreateWindowW(WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                          0, 0, 100, 28, hwnd, (HMENU)ID_TAB, g.hi, 0);
    g.status = CreateWindowW(L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE,
                             0, 0, 100, 22, hwnd, (HMENU)ID_STATUS, g.hi, 0);

    all[n++] = g.cmb_kind; all[n++] = g.edit_host; all[n++] = g.edit_port;
    all[n++] = g.edit_user; all[n++] = g.edit_pass; all[n++] = g.edit_com;
    all[n++] = g.edit_baud; all[n++] = g.btn_connect; all[n++] = g.btn_close;
    all[n++] = g.tab; all[n++] = g.status;
    all[n++] = g.lbl_host; all[n++] = g.lbl_port; all[n++] = g.lbl_user;
    all[n++] = g.lbl_pass; all[n++] = g.lbl_com; all[n++] = g.lbl_baud;
    for (i = 0; i < n; i++) SendMessageW(all[i], WM_SETFONT, (WPARAM)font, TRUE);

    ui_bridge_set_id(g.cmb_kind, "cmb.kind");
    ui_bridge_set_id(g.edit_host, "edit.ssh.host");
    ui_bridge_set_id(g.edit_port, "edit.ssh.port");
    ui_bridge_set_id(g.edit_user, "edit.ssh.user");
    ui_bridge_set_id(g.edit_pass, "edit.ssh.pass");
    ui_bridge_set_id(g.edit_com, "edit.serial.com");
    ui_bridge_set_id(g.edit_baud, "edit.serial.baud");
    ui_bridge_set_id(g.btn_connect, "btn.connect");
    ui_bridge_set_id(g.btn_close, "btn.close");
    ui_bridge_set_id(g.tab, "tab.main");
    ui_bridge_set_id(g.status, "label.status");
}

static HMENU build_menu(void)
{
    HMENU bar = CreateMenu(), f = CreatePopupMenu();
    AppendMenuW(f, MF_STRING, IDM_NEW_LOCAL, L"Connect Local");
    AppendMenuW(f, MF_STRING, IDM_NEW_SSH, L"Connect SSH (fill bar)");
    AppendMenuW(f, MF_STRING, IDM_NEW_SER, L"Connect Serial (fill bar)");
    AppendMenuW(f, MF_SEPARATOR, 0, NULL);
    AppendMenuW(f, MF_STRING, IDM_SAVE_SESS, L"Save Sessions");
    AppendMenuW(f, MF_STRING, IDM_LOAD_SESS, L"Load Sessions");
    AppendMenuW(f, MF_STRING, IDM_LOG_TOGGLE, L"Toggle Session Log");
    AppendMenuW(f, MF_SEPARATOR, 0, NULL);
    AppendMenuW(f, MF_STRING, IDM_CLOSE_TAB, L"Close Tab");
    AppendMenuW(f, MF_STRING, IDM_EXIT, L"Exit");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)f, L"Session");
    return bar;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ui_bridge_handle_message(hwnd, msg, wp, lp)) return 0;
    switch (msg) {
    case WM_CREATE:
        create_ui(hwnd);
        ui_bridge_start(hwnd, 8086);
        ui_bridge_set_state_provider(on_state, NULL);
        do_connect();
        return 0;
    case WM_SIZE: layout(hwnd); return 0;
    case WM_NOTIFY:
        if (((LPNMHDR)lp)->idFrom == ID_TAB && ((LPNMHDR)lp)->code == TCN_SELCHANGE) {
            layout(hwnd); return 0;
        }
        break;
    case WM_COMMAND:
        if (HIWORD(wp) == CBN_SELCHANGE && LOWORD(wp) == ID_CMB_KIND) {
            sync_kind_ui();
            return 0;
        }
        switch (LOWORD(wp)) {
        case ID_BTN_CONNECT: do_connect(); break;
        case IDM_NEW_LOCAL:
            SendMessageW(g.cmb_kind, CB_SETCURSEL, 0, 0); sync_kind_ui(); do_connect(); break;
        case IDM_NEW_SSH:
            SendMessageW(g.cmb_kind, CB_SETCURSEL, 1, 0); sync_kind_ui(); do_connect(); break;
        case IDM_NEW_SER:
            SendMessageW(g.cmb_kind, CB_SETCURSEL, 2, 0); sync_kind_ui(); do_connect(); break;
        case ID_BTN_CLOSE: case IDM_CLOSE_TAB: close_tab(cur_tab()); break;
        case IDM_LOG_TOGGLE: toggle_log(); break;
        case IDM_SAVE_SESS:
            yt_sessions_save(g.sess_path, g.sess, g.ntabs);
            set_status(L"Sessions saved");
            break;
        case IDM_LOAD_SESS: {
            YtSession loaded[YT_MAX_TABS];
            int n = yt_sessions_load(g.sess_path, loaded, YT_MAX_TABS), i;
            for (i = 0; i < n; i++) {
                YtSession *s = (YtSession *)calloc(1, sizeof(YtSession));
                *s = loaded[i];
                InitializeCriticalSection(&s->lock);
                if (s->kind == YT_KIND_SSH) start_ok_or_err(s, yt_session_start_ssh(s));
                else if (s->kind == YT_KIND_SERIAL) start_ok_or_err(s, yt_session_start_serial(s));
                else start_ok_or_err(s, yt_session_start_local(s, s->shell));
            }
            set_status(L"Sessions loaded");
            break;
        }
        case IDM_EXIT: DestroyWindow(hwnd); break;
        }
        return 0;
    case WM_YT_CLOSED:
        set_status(L"Session closed");
        return 0;
    case WM_DESTROY:
        while (g.ntabs > 0) close_tab(0);
        ui_bridge_stop();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE prev, PWSTR cmd, int show)
{
    WNDCLASSW wc = {0};
    HWND hwnd;
    MSG msg;
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES };
    wchar_t *slash;
    (void)prev; (void)cmd;

    InitCommonControlsEx(&icc);
    g.hi = hi;
    GetModuleFileNameW(NULL, g.exe_dir, YT_PATH_MAX);
    slash = wcsrchr(g.exe_dir, L'\\');
    if (slash) *slash = 0;
    yt_set_askpass_dir(g.exe_dir);
    swprintf(g.sess_path, YT_PATH_MAX, L"%ls\\sessions.json", g.exe_dir);

    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"YTerminalMain";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassW(&wc);

    hwnd = CreateWindowExW(0, L"YTerminalMain", L"YTerminal",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, 1000, 640,
                           NULL, build_menu(), hi, NULL);
    g.hwnd = hwnd;
    ShowWindow(hwnd, show);

    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
