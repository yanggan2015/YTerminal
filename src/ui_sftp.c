#include "app.h"
#include "ids.h"
#include "ui_term.h"
#include <commdlg.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    HWND hwnd, path, list, status;
    Session s;
    char cwd[512];
    int busy;
} SftpWnd;

static SftpWnd *g_sftp = NULL;

/* ---------------- 进程输出捕获 ---------------- */
static int run_capture(const wchar_t *cmd, char *out, int cap, int want_stderr) {
    out[0] = 0;
    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rd = NULL, wr = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = want_stderr ? wr : wr;
    si.hStdInput = NULL;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    wchar_t *cmdline = (wchar_t *)xmalloc((wcslen(cmd) + 8) * sizeof(wchar_t));
    wcscpy(cmdline, cmd);
    BOOL ok = CreateProcessW(NULL, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cmdline);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return -2; }

    int used = 0;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(rd, buf, sizeof(buf), &n, NULL) && n > 0) {
        if (used + (int)n < cap - 1) {
            memcpy(out + used, buf, n);
            used += (int)n;
        }
    }
    out[used] = 0;
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(rd);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

static wchar_t g_exe_dir[MAX_PATH] = { 0 };
static void ensure_exe_dir(void) {
    if (g_exe_dir[0]) return;
    GetModuleFileNameW(NULL, g_exe_dir, MAX_PATH);
    wchar_t *slash = wcsrchr(g_exe_dir, L'\\');
    if (slash) *slash = 0;
}

static void set_askpass_env(Session *s) {
    ensure_exe_dir();
    if (s->auth == AUTH_PASSWORD && s->password[0]) {
        wchar_t helper[MAX_PATH];
        _snwprintf(helper, MAX_PATH, L"%s\\YTPass.exe", g_exe_dir);
        SetEnvironmentVariableW(L"SSH_ASKPASS", helper);
        SetEnvironmentVariableW(L"SSH_ASKPASS_REQUIRE", L"force");
        wchar_t *pw = utf8_to_wide(s->password);
        SetEnvironmentVariableW(L"YT_ASKPASS", pw);
        free(pw);
    } else {
        SetEnvironmentVariableW(L"SSH_ASKPASS", NULL);
        SetEnvironmentVariableW(L"SSH_ASKPASS_REQUIRE", NULL);
        SetEnvironmentVariableW(L"YT_ASKPASS", NULL);
    }
}

static void ssh_base(Session *s, wchar_t *buf, int cch, int sftp_mode) {
    wchar_t *user = utf8_to_wide(s->user);
    wchar_t *host = utf8_to_wide(s->host);
    wchar_t *key = utf8_to_wide(s->keyfile);
    int batch = !(s->auth == AUTH_PASSWORD && s->password[0]);
    _snwprintf(buf, cch,
               L"ssh.exe -p %d -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10 "
               L"-o BatchMode=%s %s%s%s %s@%s ",
               s->port > 0 ? s->port : 22,
               batch ? L"yes" : L"no",
               (s->auth == AUTH_KEY && key && key[0]) ? L"-i " : L"",
               (s->auth == AUTH_KEY && key && key[0]) ? key : L"",
               (s->auth == AUTH_KEY && key && key[0]) ? L" " : L"",
               user ? user : L"", host ? host : L"");
    buf[cch - 1] = 0;
    free(user); free(host); free(key);
}

static void scp_base(Session *s, wchar_t *buf, int cch) {
    wchar_t *key = utf8_to_wide(s->keyfile);
    int batch = !(s->auth == AUTH_PASSWORD && s->password[0]);
    _snwprintf(buf, cch,
               L"scp.exe -P %d -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10 "
               L"-o BatchMode=%s %s%s%s ",
               s->port > 0 ? s->port : 22,
               batch ? L"yes" : L"no",
               (s->auth == AUTH_KEY && key && key[0]) ? L"-i " : L"",
               (s->auth == AUTH_KEY && key && key[0]) ? key : L"",
               (s->auth == AUTH_KEY && key && key[0]) ? L" " : L"");
    buf[cch - 1] = 0;
    free(key);
}

static void sftp_status(SftpWnd *sf, const wchar_t *text) {
    SetWindowTextW(sf->status, text);
}

static void sftp_list(SftpWnd *sf) {
    if (sf->busy) return;
    sf->busy = 1;
    sftp_status(sf, L"正在读取目录…");
    UpdateWindow(sf->status);
    ListBox_ResetContent(sf->list);
    ListBox_AddString(sf->list, L"..");

    wchar_t base[1024];
    ssh_base(&sf->s, base, 1024, 0);
    wchar_t wcwd[512];
    wchar_t *cw = utf8_to_wide(sf->cwd);
    wcsncpy(wcwd, cw ? cw : L"/", 511); wcwd[511] = 0;
    free(cw);
    wchar_t cmd[2048];
    _snwprintf(cmd, 2048, L"%s\"cd '%s' && ls -1Ap\" 2>&1", base, wcwd);
    cmd[2047] = 0;
    set_askpass_env(&sf->s);
    char *out = (char *)xcalloc(1, 65536);
    int code = run_capture(cmd, out, 65536, 1);
    set_askpass_env(&sf->s);

    char *save = NULL;
    int count = 0;
    for (char *line = strtok_s(out, "\r\n", &save); line; line = strtok_s(NULL, "\r\n", &save)) {
        yt_trim(line);
        if (!*line) continue;
        if (!strncmp(line, "total", 5)) continue;
        if (strstr(line, "Permission denied") || strstr(line, "Connection refused") ||
            strstr(line, "Could not resolve") || strstr(line, "Host key verification failed") ||
            strstr(line, "password") || strstr(line, "Pseudo-terminal")) {
            sftp_status(sf, (wchar_t *)utf8_to_wide(line));
            continue;
        }
        wchar_t *w = utf8_to_wide(line);
        ListBox_AddString(sf->list, w);
        free(w);
        count++;
    }
    free(out);
    wchar_t st[256];
    if (code != 0 && count == 0) {
        _snwprintf(st, 256, L"读取失败（退出码 %d）。可改用密钥/Agent 认证。", code);
        sftp_status(sf, st);
    } else {
        _snwprintf(st, 256, L"%s  ·  %d 项", wcwd, count);
        sftp_status(sf, st);
    }
    sf->busy = 0;
}

static int sel_name(SftpWnd *sf, char *out, int cch) {
    int idx = (int)SendMessageW(sf->list, LB_GETCURSEL, 0, 0);
    if (idx < 0) return 0;
    wchar_t w[512] = { 0 };
    SendMessageW(sf->list, LB_GETTEXT, idx, (LPARAM)w);
    char *u = wide_to_utf8(w);
    if (!u) return 0;
    strncpy(out, u, cch - 1);
    out[cch - 1] = 0;
    free(u);
    return 1;
}

static void sftp_upload(SftpWnd *sf) {
    wchar_t path[MAX_PATH * 2] = { 0 };
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = sf->hwnd;
    ofn.lpstrFilter = L"所有文件\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    wchar_t base[1024];
    scp_base(&sf->s, base, 1024);
    wchar_t *user = utf8_to_wide(sf->s.user);
    wchar_t *host = utf8_to_wide(sf->s.host);
    wchar_t wcwd[512];
    wchar_t *cw = utf8_to_wide(sf->cwd);
    wcsncpy(wcwd, cw ? cw : L"/", 511); wcwd[511] = 0;
    free(cw);
    wchar_t cmd[2048];
    _snwprintf(cmd, 2048, L"%s\"%s\" \"%s@%s:%s/\"", base, path, user, host, wcwd);
    cmd[2047] = 0;
    set_askpass_env(&sf->s);
    char *out = (char *)xcalloc(1, 8192);
    int code = run_capture(cmd, out, 8192, 1);
    set_askpass_env(&sf->s);
    wchar_t st[256];
    if (code == 0) wcscpy(st, L"上传完成。");
    else {
        wchar_t *oe = utf8_to_wide(out);
        _snwprintf(st, 256, L"上传失败：%s", oe ? oe : L"");
        free(oe);
    }
    sftp_status(sf, st);
    free(out);
    free(user); free(host);
    sftp_list(sf);
}

static void sftp_download(SftpWnd *sf) {
    char name[512] = { 0 };
    if (!sel_name(sf, name, 512)) return;
    if (!strcmp(name, "..")) return;
    wchar_t dir[MAX_PATH * 2];
    wcsncpy(dir, g_app.set.download_dir, MAX_PATH * 2 - 1);
    if (!dir[0]) wcscpy(dir, L"downloads");
    yt_mkdir(dir);
    wchar_t *user = utf8_to_wide(sf->s.user);
    wchar_t *host = utf8_to_wide(sf->s.host);
    wchar_t base[1024];
    scp_base(&sf->s, base, 1024);
    wchar_t wcwd[512];
    wchar_t *cw = utf8_to_wide(sf->cwd);
    wcsncpy(wcwd, cw ? cw : L"/", 511); wcwd[511] = 0;
    free(cw);
    wchar_t wname[512];
    wchar_t *wn = utf8_to_wide(name);
    wcsncpy(wname, wn ? wn : L"", 511); wname[511] = 0;
    free(wn);
    wchar_t cmd[2048];
    _snwprintf(cmd, 2048, L"%s-a \"%s@%s:%s/%s\" \"%s\"", base, user, host, wcwd, wname, dir);
    cmd[2047] = 0;
    set_askpass_env(&sf->s);
    char *out = (char *)xcalloc(1, 8192);
    int code = run_capture(cmd, out, 8192, 1);
    set_askpass_env(&sf->s);
    wchar_t st[512];
    if (code == 0) _snwprintf(st, 512, L"已下载到 %s", dir);
    else {
        wchar_t *oe = utf8_to_wide(out);
        _snwprintf(st, 512, L"下载失败：%s", oe ? oe : L"");
        free(oe);
    }
    sftp_status(sf, st);
    free(out);
    free(user); free(host);
}

static void sftp_delete(SftpWnd *sf) {
    char name[512] = { 0 };
    if (!sel_name(sf, name, 512)) return;
    if (!strcmp(name, "..")) return;
    if (MessageBoxW(sf->hwnd, L"确定删除该远程文件/目录？", L"YTerminal", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    wchar_t base[1024];
    ssh_base(&sf->s, base, 1024, 0);
    wchar_t wcwd[512], wname[512];
    wchar_t *cw = utf8_to_wide(sf->cwd);
    wcsncpy(wcwd, cw ? cw : L"/", 511); wcwd[511] = 0; free(cw);
    wchar_t *wn = utf8_to_wide(name);
    wcsncpy(wname, wn ? wn : L"", 511); wname[511] = 0; free(wn);
    wchar_t cmd[2048];
    _snwprintf(cmd, 2048, L"%s\"rm -rf -- '%s/%s'\" 2>&1", base, wcwd, wname);
    cmd[2047] = 0;
    set_askpass_env(&sf->s);
    char *out = (char *)xcalloc(1, 4096);
    int code = run_capture(cmd, out, 4096, 1);
    set_askpass_env(&sf->s);
    sftp_status(sf, code == 0 ? L"已删除。" : L"删除失败。");
    free(out);
    sftp_list(sf);
}

static void sftp_enter(SftpWnd *sf) {
    char name[512] = { 0 };
    if (!sel_name(sf, name, 512)) return;
    if (!strcmp(name, "..")) {
        char tmp[512];
        strncpy(tmp, sf->cwd, 511);
        tmp[511] = 0;
        char *slash = strrchr(tmp, '/');
        if (slash && slash != tmp) *slash = 0;
        else if (slash == tmp) tmp[1] = 0;
        strcpy(sf->cwd, tmp);
        sftp_list(sf);
        return;
    }
    size_t n = strlen(name);
    if (n && name[n - 1] == '/') {
        char next[512];
        _snprintf(next, 512, "%s%s%s", sf->cwd, sf->cwd[0] == '/' && sf->cwd[1] == 0 ? "" : "/", name);
        next[511] = 0;
        strcpy(sf->cwd, next);
        sftp_list(sf);
    } else {
        sftp_status(sf, L"双击目录进入；选中文件可下载。");
    }
}

static void sftp_layout(SftpWnd *sf) {
    RECT rc;
    GetClientRect(sf->hwnd, &rc);
    int h = rc.bottom;
    MoveWindow(sf->path, 10, 10, rc.right - 310, 28, TRUE);
    MoveWindow(sf->list, 10, 48, rc.right - 20, h - 140, TRUE);
    MoveWindow(sf->status, 10, h - 82, rc.right - 20, 24, TRUE);
    /* 按钮在标题行：刷新/上传/下载/删除 */
    (void)sf;
}

static void sftp_paint(SftpWnd *sf) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(sf->hwnd, &ps);
    RECT rc; GetClientRect(sf->hwnd, &rc);
    app_fill(hdc, &rc, app_ui_bg());
    EndPaint(sf->hwnd, &ps);
}

static LRESULT CALLBACK SftpProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    SftpWnd *sf = (SftpWnd *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        sf = (SftpWnd *)cs->lpCreateParams;
        sf->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)sf);

        int w = 760;
        sf->path = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"/", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                   10, 10, w - 310, 28, h, (HMENU)4001, g_app.inst, NULL);
        SendMessageW(sf->path, WM_SETFONT, (WPARAM)g_app.f_ui, TRUE);
        struct { int id; const wchar_t *text; int x; } btns[] = {
            { 4002, L"刷新", 0 }, { 4003, L"上传", 0 }, { 4004, L"下载", 0 }, { 4005, L"删除", 0 },
        };
        int bx = 10 + (w - 310) + 8;
        for (int i = 0; i < 4; i++) {
            HWND b = CreateWindowExW(0, L"BUTTON", btns[i].text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     bx, 10, 64, 28, h, (HMENU)(INT_PTR)btns[i].id, g_app.inst, NULL);
            SendMessageW(b, WM_SETFONT, (WPARAM)g_app.f_ui, TRUE);
            bx += 70;
        }
        sf->list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                   10, 48, w - 20, 350, h, (HMENU)4006, g_app.inst, NULL);
        SendMessageW(sf->list, WM_SETFONT, (WPARAM)g_app.f_latin, TRUE);
        sf->status = CreateWindowExW(0, L"STATIC", L"就绪", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                     10, 410, w - 20, 24, h, (HMENU)4007, g_app.inst, NULL);
        SendMessageW(sf->status, WM_SETFONT, (WPARAM)g_app.f_ui, TRUE);
        SetDlgItemTextW(h, 4001, L"/");
        return 0;
    }
    case WM_SIZE: sftp_layout(sf); return 0;
    case WM_PAINT: sftp_paint(sf); return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp; SetTextColor(dc, app_ui_dim()); SetBkColor(dc, app_ui_bg());
        static HBRUSH br = NULL;
        if (!br) br = CreateSolidBrush(app_ui_bg());
        return (LRESULT)br;
    }
    case WM_ERASEBKGND: return 1;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == 4002) {
            wchar_t wt[512];
            GetDlgItemTextW(h, 4001, wt, 512);
            char *u = wide_to_utf8(wt);
            if (u && *u) { strncpy(sf->cwd, u, 511); sf->cwd[511] = 0; }
            free(u);
            sftp_list(sf);
        } else if (id == 4003) sftp_upload(sf);
        else if (id == 4004) sftp_download(sf);
        else if (id == 4005) sftp_delete(sf);
        else if (id == 4006 && HIWORD(wp) == LBN_DBLCLK) sftp_enter(sf);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        if (g_sftp == sf) g_sftp = NULL;
        free(sf);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void app_show_sftp(HWND parent) {
    if (g_sftp && IsWindow(g_sftp->hwnd)) {
        ShowWindow(g_sftp->hwnd, SW_SHOW);
        SetForegroundWindow(g_sftp->hwnd);
        return;
    }
    HWND v = app_active_view();
    Session *s = v ? term_view_session(v) : NULL;
    if (!s || (s->protocol != PROTO_SSH && s->protocol != PROTO_TELNET)) {
        MessageBoxW(parent, L"SFTP 仅支持 SSH 会话，请先打开一个 SSH 标签。", L"YTerminal", MB_OK | MB_ICONINFORMATION);
        return;
    }
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SftpProc;
    wc.hInstance = g_app.inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"YSftp";
    wc.hbrBackground = NULL;
    RegisterClassExW(&wc);

    SftpWnd *sf = (SftpWnd *)xcalloc(1, sizeof(SftpWnd));
    sf->s = *s;
    strcpy(sf->cwd, "/");
    HWND w = CreateWindowExW(0, L"YSftp", L"SFTP 文件传输", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 780, 540, parent, NULL, g_app.inst, sf);
    if (!w) { free(sf); return; }
    g_sftp = sf;
    ShowWindow(w, SW_SHOW);
    sftp_list(sf);
}
