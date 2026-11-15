#include "yt_core.h"
#include "win_ui_bridge.h"
#include <commdlg.h>
#include <ctype.h>
#include <wctype.h>

#define IDC_P_NAME 7001
#define IDC_P_HOST 7002
#define IDC_P_PORT 7003
#define IDC_P_USER 7004
#define IDC_P_PASS 7005
#define IDC_P_COM  7006
#define IDC_P_BAUD 7007
#define IDC_P_KIND 7008
#define IDC_P_KEY  7009
#define IDC_P_JUMP 7010
#define IDC_P_FWD  7011
#define IDC_P_BROWSE 7012
#define IDC_P_SHELL 7013

typedef struct {
    YtSession *s;
    int done, ok;
    HWND e_name, e_host, e_port, e_user, e_pass, e_com, e_baud, e_key, e_jump, e_fwd, cmb, cmb_shell;
    HWND l_host, l_port, l_user, l_pass, l_key, l_jump, l_fwd, l_com, l_baud, l_shell, btn_browse;
} PropCtx;

static int default_port(int kind)
{
    if (kind == YT_KIND_SSH) return 22;
    if (kind == YT_KIND_TELNET) return 23;
    return 0;
}

static void fill_com_ports(HWND cmb)
{
    wchar_t buf[4096], name[64];
    DWORD n;
    int i;
    SendMessageW(cmb, CB_RESETCONTENT, 0, 0);
    n = QueryDosDeviceW(NULL, buf, 4096);
    if (n) {
        for (wchar_t *p = buf; *p; p += wcslen(p) + 1) {
            if (_wcsnicmp(p, L"COM", 3) == 0 && iswdigit(p[3]))
                SendMessageW(cmb, CB_ADDSTRING, 0, (LPARAM)p);
        }
    }
    if (SendMessageW(cmb, CB_GETCOUNT, 0, 0) == 0) {
        for (i = 1; i <= 20; i++) {
            swprintf(name, 64, L"COM%d", i);
            SendMessageW(cmb, CB_ADDSTRING, 0, (LPARAM)name);
        }
    }
}

static void fill_baud(HWND cmb)
{
    static const wchar_t *rates[] = {
        L"9600", L"19200", L"38400", L"57600", L"115200",
        L"230400", L"460800", L"921600", L"1500000", NULL
    };
    int i;
    SendMessageW(cmb, CB_RESETCONTENT, 0, 0);
    for (i = 0; rates[i]; i++) SendMessageW(cmb, CB_ADDSTRING, 0, (LPARAM)rates[i]);
}

static void show_ctl(HWND h, BOOL on)
{
    if (h) ShowWindow(h, on ? SW_SHOW : SW_HIDE);
}

static void apply_kind_ui(PropCtx *ctx, int kind, BOOL set_port_default)
{
    BOOL ssh = (kind == YT_KIND_SSH);
    BOOL ser = (kind == YT_KIND_SERIAL);
    BOOL tel = (kind == YT_KIND_TELNET);
    BOOL loc = (kind == YT_KIND_LOCAL);
    BOOL net = ssh || tel;

    show_ctl(ctx->l_host, net); show_ctl(ctx->e_host, net);
    show_ctl(ctx->l_port, net); show_ctl(ctx->e_port, net);
    show_ctl(ctx->l_user, net); show_ctl(ctx->e_user, net);
    show_ctl(ctx->l_pass, net); show_ctl(ctx->e_pass, net);
    show_ctl(ctx->l_key, ssh); show_ctl(ctx->e_key, ssh); show_ctl(ctx->btn_browse, ssh);
    show_ctl(ctx->l_jump, ssh); show_ctl(ctx->e_jump, ssh);
    show_ctl(ctx->l_fwd, ssh); show_ctl(ctx->e_fwd, ssh);
    show_ctl(ctx->l_com, ser); show_ctl(ctx->e_com, ser);
    show_ctl(ctx->l_baud, ser); show_ctl(ctx->e_baud, ser);
    show_ctl(ctx->l_shell, loc); show_ctl(ctx->cmb_shell, loc);

    if (set_port_default && net) {
        wchar_t b[16];
        swprintf(b, 16, L"%d", default_port(kind));
        SetWindowTextW(ctx->e_port, b);
    }
}

static HWND mk_static(HWND hw, const wchar_t *t, int x, int y, int w)
{
    return CreateWindowW(L"STATIC", t, WS_CHILD | WS_VISIBLE, x, y, w, 20, hw, 0, NULL, 0);
}

static LRESULT CALLBACK PropProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    PropCtx *ctx = (PropCtx *)GetWindowLongPtrW(hw, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        wchar_t b[32];
        int kind;
        ctx = (PropCtx *)cs->lpCreateParams;
        SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)ctx);

        mk_static(hw, L"名称", 16, 16, 50);
        ctx->e_name = CreateWindowW(L"EDIT", ctx->s->name, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                    80, 14, 260, 24, hw, (HMENU)IDC_P_NAME, NULL, 0);
        mk_static(hw, L"类型", 16, 48, 50);
        ctx->cmb = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                 80, 46, 140, 200, hw, (HMENU)IDC_P_KIND, NULL, 0);
        SendMessageW(ctx->cmb, CB_ADDSTRING, 0, (LPARAM)L"Local");
        SendMessageW(ctx->cmb, CB_ADDSTRING, 0, (LPARAM)L"SSH");
        SendMessageW(ctx->cmb, CB_ADDSTRING, 0, (LPARAM)L"Serial");
        SendMessageW(ctx->cmb, CB_ADDSTRING, 0, (LPARAM)L"Telnet");
        kind = (int)ctx->s->kind;
        if (kind < 0 || kind > 3) kind = 1;
        SendMessageW(ctx->cmb, CB_SETCURSEL, (WPARAM)kind, 0);

        ctx->l_shell = mk_static(hw, L"Shell", 16, 80, 50);
        ctx->cmb_shell = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST,
                                       80, 78, 140, 120, hw, (HMENU)IDC_P_SHELL, NULL, 0);
        SendMessageW(ctx->cmb_shell, CB_ADDSTRING, 0, (LPARAM)L"powershell");
        SendMessageW(ctx->cmb_shell, CB_ADDSTRING, 0, (LPARAM)L"cmd");
        SendMessageW(ctx->cmb_shell, CB_SETCURSEL,
                     (_wcsicmp(ctx->s->shell, L"cmd") == 0) ? 1 : 0, 0);

        ctx->l_host = mk_static(hw, L"Host", 16, 80, 50);
        ctx->e_host = CreateWindowW(L"EDIT", ctx->s->host, WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                                    80, 78, 260, 24, hw, (HMENU)IDC_P_HOST, NULL, 0);
        ctx->l_port = mk_static(hw, L"Port", 16, 112, 50);
        swprintf(b, 32, L"%d", ctx->s->port ? ctx->s->port : default_port(kind));
        ctx->e_port = CreateWindowW(L"EDIT", b, WS_CHILD | WS_BORDER, 80, 110, 80, 24, hw, (HMENU)IDC_P_PORT, NULL, 0);
        ctx->l_user = mk_static(hw, L"User", 16, 144, 50);
        ctx->e_user = CreateWindowW(L"EDIT", ctx->s->user, WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                                    80, 142, 120, 24, hw, (HMENU)IDC_P_USER, NULL, 0);
        ctx->l_pass = mk_static(hw, L"Pass", 210, 144, 40);
        ctx->e_pass = CreateWindowW(L"EDIT", ctx->s->pass, WS_CHILD | WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL,
                                    250, 142, 90, 24, hw, (HMENU)IDC_P_PASS, NULL, 0);
        ctx->l_key = mk_static(hw, L"密钥", 16, 176, 50);
        ctx->e_key = CreateWindowW(L"EDIT", ctx->s->keyfile, WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                                   80, 174, 200, 24, hw, (HMENU)IDC_P_KEY, NULL, 0);
        ctx->btn_browse = CreateWindowW(L"BUTTON", L"...", WS_CHILD, 290, 174, 40, 24, hw, (HMENU)IDC_P_BROWSE, NULL, 0);
        ctx->l_jump = mk_static(hw, L"跳板", 16, 208, 50);
        ctx->e_jump = CreateWindowW(L"EDIT", ctx->s->jump_host, WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                                    80, 206, 260, 24, hw, (HMENU)IDC_P_JUMP, NULL, 0);
        ctx->l_fwd = mk_static(hw, L"转发", 16, 240, 50);
        {
            wchar_t fw[512];
            MultiByteToWideChar(CP_UTF8, 0, ctx->s->forwards, -1, fw, 512);
            ctx->e_fwd = CreateWindowW(L"EDIT", fw, WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                                       80, 238, 260, 24, hw, (HMENU)IDC_P_FWD, NULL, 0);
        }
        ctx->l_com = mk_static(hw, L"COM", 16, 80, 50);
        ctx->e_com = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWN | WS_VSCROLL,
                                   80, 78, 100, 200, hw, (HMENU)IDC_P_COM, NULL, 0);
        fill_com_ports(ctx->e_com);
        if (ctx->s->com[0]) {
            if (SendMessageW(ctx->e_com, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)ctx->s->com) == CB_ERR)
                SendMessageW(ctx->e_com, CB_ADDSTRING, 0, (LPARAM)ctx->s->com);
            SetWindowTextW(ctx->e_com, ctx->s->com);
        }
        ctx->l_baud = mk_static(hw, L"波特率", 190, 80, 50);
        ctx->e_baud = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWN | WS_VSCROLL,
                                    250, 78, 90, 200, hw, (HMENU)IDC_P_BAUD, NULL, 0);
        fill_baud(ctx->e_baud);
        swprintf(b, 32, L"%d", ctx->s->baud ? ctx->s->baud : 115200);
        SetWindowTextW(ctx->e_baud, b);

        CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 80, 310, 90, 28, hw, (HMENU)IDOK, NULL, 0);
        CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE, 190, 310, 90, 28, hw, (HMENU)IDCANCEL, NULL, 0);

        apply_kind_ui(ctx, kind, FALSE);
        ui_bridge_set_id(ctx->e_name, "edit.prop.name");
        ui_bridge_set_id(ctx->e_host, "edit.prop.host");
        ui_bridge_set_id(ctx->e_key, "edit.prop.key");
        ui_bridge_set_id(ctx->cmb, "cmb.prop.kind");
        return 0;
    }
    case WM_COMMAND:
        if (!ctx) return 0;
        if (LOWORD(wp) == IDC_P_KIND && HIWORD(wp) == CBN_SELCHANGE) {
            apply_kind_ui(ctx, (int)SendMessageW(ctx->cmb, CB_GETCURSEL, 0, 0), TRUE);
            return 0;
        }
        if (LOWORD(wp) == IDC_P_BROWSE) {
            OPENFILENAMEW ofn = {0}; wchar_t file[YT_PATH_MAX] = {0};
            ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hw; ofn.lpstrFile = file; ofn.nMaxFile = YT_PATH_MAX;
            ofn.lpstrFilter = L"Key Files\0*.pem;*.ppk;*\0All\0*.*\0";
            ofn.Flags = OFN_FILEMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) SetWindowTextW(ctx->e_key, file);
            return 0;
        }
        if (LOWORD(wp) == IDOK) {
            wchar_t fw[512]; char f8[512];
            int kind = (int)SendMessageW(ctx->cmb, CB_GETCURSEL, 0, 0);
            GetWindowTextW(ctx->e_name, ctx->s->name, YT_TITLE_MAX);
            GetWindowTextW(ctx->e_host, ctx->s->host, YT_HOST_MAX);
            { wchar_t pb[16]; GetWindowTextW(ctx->e_port, pb, 16); ctx->s->port = _wtoi(pb); }
            GetWindowTextW(ctx->e_user, ctx->s->user, 64);
            GetWindowTextW(ctx->e_pass, ctx->s->pass, 128);
            GetWindowTextW(ctx->e_key, ctx->s->keyfile, YT_PATH_MAX);
            GetWindowTextW(ctx->e_jump, ctx->s->jump_host, YT_HOST_MAX);
            GetWindowTextW(ctx->e_fwd, fw, 512);
            WideCharToMultiByte(CP_UTF8, 0, fw, -1, f8, sizeof(f8), NULL, NULL);
            strncpy(ctx->s->forwards, f8, sizeof(ctx->s->forwards) - 1);
            GetWindowTextW(ctx->e_com, ctx->s->com, 32);
            { wchar_t pb[16]; GetWindowTextW(ctx->e_baud, pb, 16); ctx->s->baud = _wtoi(pb); }
            {
                int si = (int)SendMessageW(ctx->cmb_shell, CB_GETCURSEL, 0, 0);
                wcscpy(ctx->s->shell, si == 1 ? L"cmd" : L"powershell");
            }
            ctx->s->kind = (YtKind)kind;
            if ((kind == YT_KIND_SSH || kind == YT_KIND_TELNET) && !ctx->s->host[0]) {
                MessageBoxW(hw, L"请填写 Host", L"YTerminal", MB_ICONWARNING);
                return 0;
            }
            if (kind == YT_KIND_SERIAL && !ctx->s->com[0]) {
                MessageBoxW(hw, L"请选择 COM 口", L"YTerminal", MB_ICONWARNING);
                return 0;
            }
            if (!ctx->s->port) ctx->s->port = default_port(kind);
            if (!ctx->s->baud) ctx->s->baud = 115200;
            ctx->ok = 1; ctx->done = 1; DestroyWindow(hw); return 0;
        }
        if (LOWORD(wp) == IDCANCEL) { ctx->done = 1; DestroyWindow(hw); return 0; }
        return 0;
    case WM_CLOSE: if (ctx) ctx->done = 1; DestroyWindow(hw); return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

int yt_dlg_session_prop(HWND parent, HINSTANCE hi, YtSession *s)
{
    static BOOL reg = FALSE;
    PropCtx ctx = {0};
    HWND hw; MSG msg; RECT pr;
    ctx.s = s;
    if (!reg) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = PropProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"YTermPropDlg";
        RegisterClassW(&wc);
        reg = TRUE;
    }
    GetWindowRect(parent, &pr);
    hw = CreateWindowExW(WS_EX_DLGMODALFRAME, L"YTermPropDlg", L"快速连接 / 会话属性",
                         WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                         pr.left + 100, pr.top + 60, 400, 390, parent, NULL, hi, &ctx);
    EnableWindow(parent, FALSE);
    while (!ctx.done && GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hw, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    return ctx.ok;
}
