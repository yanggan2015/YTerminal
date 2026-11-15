#include "app.h"
#include "ids.h"
#include "ui_term.h"
#include <commdlg.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 双栏 SFTP 管理器：左本地 / 右远端，选中即入队，后台线程顺序传输。 */

#define SF_ID_PATH_L   4101
#define SF_ID_PATH_R   4102
#define SF_ID_LIST_L   4103
#define SF_ID_LIST_R   4104
#define SF_ID_STATUS   4105
#define SF_ID_PROGRESS 4106
#define SF_ID_UP       4110
#define SF_ID_DOWN     4111
#define SF_ID_REFRESH  4112
#define SF_ID_DEL_L    4113
#define SF_ID_DEL_R    4114
#define SF_ID_MKDIR    4115
#define SF_ID_CLOSE    4116
#define SF_ID_HEAD_L   4107
#define SF_ID_HEAD_R   4108

#define SF_MAX_JOBS 64
#define SF_MAX_ENTRIES 400
#define SF_WM_JOB   (WM_APP + 60)
#define SF_WM_LIST  (WM_APP + 62)
#define SF_WM_DONE  (WM_APP + 61)

typedef struct {
    int up;                       /* 1=上传 0=下载 */
    wchar_t src[1024];
    wchar_t dst[1024];
} SfJob;

typedef struct {
    HWND hwnd, path_l, path_r, list_l, list_r, status, progress;
    Session s;
    wchar_t lcwd[MAX_PATH * 2];
    char rcwd[512];
    int busy;
    int listing;
    SfJob jobs[SF_MAX_JOBS];
    int job_n;
    HANDLE thread;
    HANDLE lthread;
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
    WaitForSingleObject(pi.hProcess, 60000);
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
        _snwprintf(helper, MAX_PATH, L"%ls\\YTPass.exe", g_exe_dir);
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

static void ssh_base(Session *s, wchar_t *buf, int cch) {
    wchar_t *user = utf8_to_wide(s->user);
    wchar_t *host = utf8_to_wide(s->host);
    wchar_t *key = utf8_to_wide(s->keyfile);
    int batch = !(s->auth == AUTH_PASSWORD && s->password[0]);
    _snwprintf(buf, cch,
               L"ssh.exe -p %d -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10 "
               L"-o BatchMode=%ls %ls%ls%ls %ls@%ls ",
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
               L"-o BatchMode=%ls %ls%ls%ls ",
               s->port > 0 ? s->port : 22,
               batch ? L"yes" : L"no",
               (s->auth == AUTH_KEY && key && key[0]) ? L"-i " : L"",
               (s->auth == AUTH_KEY && key && key[0]) ? key : L"",
               (s->auth == AUTH_KEY && key && key[0]) ? L" " : L"",
               s->port > 0 ? L"" : L"");
    buf[cch - 1] = 0;
    free(key);
}

/* ---------------- 列表辅助 ---------------- */
static void sf_status(SftpWnd *sf, const wchar_t *text) {
    SetWindowTextW(sf->status, text);
}

static int lb_sel_text(HWND lb, wchar_t *out, int cch) {
    int idx = (int)SendMessageW(lb, LB_GETCURSEL, 0, 0);
    if (idx < 0) return 0;
    out[0] = 0;
    SendMessageW(lb, LB_GETTEXT, idx, (LPARAM)out);
    out[cch - 1] = 0;
    return 1;
}

/* 进入上一层（本地用反斜杠、远端用斜杠） */
static void path_up(wchar_t *path, wchar_t sep) {
    size_t n = wcslen(path);
    if (n == 0) return;
    if (n && (path[n - 1] == sep || path[n - 1] == L'/' || path[n - 1] == L'\\')) path[--n] = 0;
    while (n > 0 && path[n - 1] != sep) n--;
    if (n > 0) path[n - 1] = 0;
    if (!path[0]) { path[0] = sep; path[1] = 0; }
}

/* ---------------- 本地目录 ---------------- */
static void sf_local_refresh(SftpWnd *sf) {
    ListBox_ResetContent(sf->list_l);
    ListBox_AddString(sf->list_l, L"..");
    int count = 0;
    wchar_t pat[MAX_PATH * 2];
    _snwprintf(pat, MAX_PATH * 2, L"%ls\\*", sf->lcwd);
    pat[MAX_PATH * 2 - 1] = 0;

    for (int pass = 0; pass < 2; pass++) {
        WIN32_FIND_DATAW fd;
        HANDLE hf = FindFirstFileW(pat, &fd);
        if (hf == INVALID_HANDLE_VALUE) break;
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            int isdir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if ((pass == 0) != (isdir != 0)) continue;
            if (count >= SF_MAX_ENTRIES) break;
            wchar_t line[300];
            if (isdir) _snwprintf(line, 300, L"%ls\\", fd.cFileName);
            else _snwprintf(line, 300, L"%ls", fd.cFileName);
            line[299] = 0;
            ListBox_AddString(sf->list_l, line);
            count++;
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }
    wchar_t st[320];
    _snwprintf(st, 320, L"本地：%ls  ·  %d 项", sf->lcwd, count);
    st[319] = 0;
    sf_status(sf, st);
    SetWindowTextW(sf->path_l, sf->lcwd);
}

/* ---------------- 远端目录（后台线程读取，网络慢也不冻结界面） ---------------- */
static void sf_remote_apply(SftpWnd *sf, char *out, int code) {
    char *save = NULL;
    int count = 0;
    char newcwd[512] = { 0 };
    ListBox_ResetContent(sf->list_r);
    ListBox_AddString(sf->list_r, L"..");
    for (char *line = strtok_s(out, "\r\n", &save); line; line = strtok_s(NULL, "\r\n", &save)) {
        yt_trim(line);
        if (!*line) continue;
        if (!newcwd[0] && line[0] == '/') { strncpy(newcwd, line, 511); continue; }
        if (count >= SF_MAX_ENTRIES) break;
        wchar_t *w = utf8_to_wide(line);
        if (w) { ListBox_AddString(sf->list_r, w); free(w); count++; }
    }
    if (newcwd[0]) { _snprintf(sf->rcwd, 512, "%s", newcwd); }
    wchar_t *wc = utf8_to_wide(sf->rcwd);
    SetWindowTextW(sf->path_r, wc ? wc : L"/");
    free(wc);
    wchar_t st[320];
    wchar_t *rc = utf8_to_wide(sf->rcwd);
    if (code != 0 && count == 0) {
        _snwprintf(st, 320, L"远端读取失败（退出码 %d），请确认主机/认证配置", code);
    } else {
        _snwprintf(st, 320, L"远端：%ls  ·  %d 项", rc ? rc : L"/", count);
    }
    st[319] = 0;
    free(rc);
    sf_status(sf, st);
    InvalidateRect(sf->list_r, NULL, TRUE);
}

typedef struct {
    SftpWnd *sf;
    wchar_t cmd[2048];
} SfListReq;

static DWORD WINAPI sf_list_worker(LPVOID param) {
    SfListReq *r = (SfListReq *)param;
    SftpWnd *sf = r->sf;
    char *out = (char *)xcalloc(1, 65536);
    set_askpass_env(&sf->s);
    int code = run_capture(r->cmd, out, 65536, 1);
    set_askpass_env(&sf->s);
    free(r);
    if (!PostMessageW(sf->hwnd, SF_WM_LIST, (WPARAM)code, (LPARAM)out)) free(out);
    return 0;
}

static void sf_remote_refresh(SftpWnd *sf) {
    if (sf->listing) return;
    sf->listing = 1;
    sf_status(sf, L"正在读取远端目录…");
    wchar_t base[1024];
    ssh_base(&sf->s, base, 1024);
    wchar_t cwd[512];
    wchar_t *cw = utf8_to_wide(sf->rcwd);
    wcsncpy(cwd, cw ? cw : L"/", 511); cwd[511] = 0;
    free(cw);
    SfListReq *r = (SfListReq *)xcalloc(1, sizeof(SfListReq));
    r->sf = sf;
    _snwprintf(r->cmd, 2048, L"%ls\"cd '%ls' 2>/dev/null && pwd && ls -1Ap\" 2>&1", base, cwd);
    r->cmd[2047] = 0;
    if (sf->lthread) { CloseHandle(sf->lthread); sf->lthread = NULL; }
    sf->lthread = CreateThread(NULL, 0, sf_list_worker, r, 0, NULL);
    if (!sf->lthread) { sf->listing = 0; free(r); sf_status(sf, L"无法启动读取线程"); }
}

/* ---------------- 传输队列 ---------------- */
static DWORD WINAPI sf_worker(LPVOID param) {
    SftpWnd *sf = (SftpWnd *)param;
    for (int i = 0; i < sf->job_n; i++) {
        PostMessageW(sf->hwnd, SF_WM_JOB, (WPARAM)(i + 1), (LPARAM)sf->job_n);
        SfJob *j = &sf->jobs[i];
        wchar_t cmd[2600];
        if (j->up) {
            wchar_t base[1024];
            scp_base(&sf->s, base, 1024);
            wchar_t *user = utf8_to_wide(sf->s.user);
            wchar_t *host = utf8_to_wide(sf->s.host);
            _snwprintf(cmd, 2600, L"%ls\"%ls\" \"%ls@%ls:%ls\"", base, j->src, user ? user : L"", host ? host : L"", j->dst);
            free(user); free(host);
        } else {
            wchar_t base[1024];
            scp_base(&sf->s, base, 1024);
            wchar_t *user = utf8_to_wide(sf->s.user);
            wchar_t *host = utf8_to_wide(sf->s.host);
            _snwprintf(cmd, 2600, L"%ls-a \"%ls@%ls:%ls\" \"%ls\"", base, user ? user : L"", host ? host : L"", j->src, j->dst);
            free(user); free(host);
        }
        cmd[2599] = 0;
        set_askpass_env(&sf->s);
        char *out = (char *)xcalloc(1, 8192);
        run_capture(cmd, out, 8192, 1);
        set_askpass_env(&sf->s);
        free(out);
    }
    PostMessageW(sf->hwnd, SF_WM_DONE, 0, 0);
    return 0;
}

static void sf_queue_add(SftpWnd *sf, int up, const wchar_t *src, const wchar_t *dst) {
    if (sf->job_n >= SF_MAX_JOBS) return;
    SfJob *j = &sf->jobs[sf->job_n++];
    j->up = up;
    wcsncpy(j->src, src, 1023); j->src[1023] = 0;
    wcsncpy(j->dst, dst, 1023); j->dst[1023] = 0;
}

static void sf_start(SftpWnd *sf) {
    if (sf->job_n <= 0 || sf->busy) return;
    sf->busy = 1;
    SendMessageW(sf->progress, PBM_SETMARQUEE, TRUE, 0);
    ShowWindow(sf->progress, SW_SHOW);
    wchar_t st[160];
    _snwprintf(st, 160, L"正在传输 0/%d 个文件…", sf->job_n);
    st[159] = 0;
    sf_status(sf, st);
    sf->thread = CreateThread(NULL, 0, sf_worker, sf, 0, NULL);
    if (!sf->thread) {
        sf->busy = 0;
        sf_status(sf, L"无法启动传输线程");
    }
}

/* 从本地列表选中项入队上传 */
static void sf_upload(SftpWnd *sf) {
    if (sf->busy) { sf_status(sf, L"上一批还在传输，请稍候"); return; }
    sf->job_n = 0;
    int cnt = (int)SendMessageW(sf->list_l, LB_GETSELCOUNT, 0, 0);
    if (cnt <= 0) { sf_status(sf, L"请先在左侧选中要上传的文件"); return; }
    int *idx = (int *)xcalloc((size_t)cnt, sizeof(int));
    SendMessageW(sf->list_l, LB_GETSELITEMS, cnt, (LPARAM)idx);
    for (int i = 0; i < cnt; i++) {
        wchar_t name[300] = { 0 };
        SendMessageW(sf->list_l, LB_GETTEXT, idx[i], (LPARAM)name);
        name[299] = 0;
        size_t len = wcslen(name);
        if (!len || !wcscmp(name, L"..")) continue;
        if (name[len - 1] == L'\\') {
            sf_status(sf, L"目录上传暂不支持，请逐个文件上传");
            continue;
        }
        wchar_t src[MAX_PATH * 2], dst[600];
        _snwprintf(src, MAX_PATH * 2, L"%ls\\%ls", sf->lcwd, name);
        src[MAX_PATH * 2 - 1] = 0;
        wchar_t *rc = utf8_to_wide(sf->rcwd);
        _snwprintf(dst, 600, L"%ls/%ls", rc ? rc : L"/", name);
        dst[599] = 0;
        free(rc);
        sf_queue_add(sf, 1, src, dst);
    }
    free(idx);
    sf_start(sf);
}

/* 从远端列表选中项入队下载 */
static void sf_download(SftpWnd *sf) {
    if (sf->busy) { sf_status(sf, L"上一批还在传输，请稍候"); return; }
    sf->job_n = 0;
    int cnt = (int)SendMessageW(sf->list_r, LB_GETSELCOUNT, 0, 0);
    if (cnt <= 0) { sf_status(sf, L"请先在右侧选中要下载的文件"); return; }
    int *idx = (int *)xcalloc((size_t)cnt, sizeof(int));
    SendMessageW(sf->list_r, LB_GETSELITEMS, cnt, (LPARAM)idx);
    for (int i = 0; i < cnt; i++) {
        wchar_t name[300] = { 0 };
        SendMessageW(sf->list_r, LB_GETTEXT, idx[i], (LPARAM)name);
        name[299] = 0;
        size_t len = wcslen(name);
        if (!len || !wcscmp(name, L"..")) continue;
        if (name[len - 1] == L'/') {
            sf_status(sf, L"目录下载暂不支持，请先进入目录再下载文件");
            continue;
        }
        wchar_t dst[MAX_PATH * 2];
        _snwprintf(dst, MAX_PATH * 2, L"%ls\\%ls", sf->lcwd, name);
        dst[MAX_PATH * 2 - 1] = 0;
        wchar_t *rc = utf8_to_wide(sf->rcwd);
        wchar_t src[600];
        _snwprintf(src, 600, L"%ls/%ls", rc ? rc : L"/", name);
        src[599] = 0;
        free(rc);
        sf_queue_add(sf, 0, src, dst);
    }
    free(idx);
    sf_start(sf);
}

/* ---------------- 删除 ---------------- */
static void sf_delete_local(SftpWnd *sf) {
    wchar_t name[300] = { 0 };
    if (!lb_sel_text(sf->list_l, name, 300) || !wcscmp(name, L"..")) return;
    if (MessageBoxW(sf->hwnd, L"确定删除选中的本地文件/目录？", L"YTerminal", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    wchar_t full[MAX_PATH * 2];
    _snwprintf(full, MAX_PATH * 2, L"%ls\\%ls", sf->lcwd, name);
    full[MAX_PATH * 2 - 1] = 0;
    size_t len = wcslen(name);
    if (len && name[len - 1] == L'\\') RemoveDirectoryW(full);
    else DeleteFileW(full);
    sf_local_refresh(sf);
}

static void sf_delete_remote(SftpWnd *sf) {
    wchar_t name[300] = { 0 };
    if (!lb_sel_text(sf->list_r, name, 300) || !wcscmp(name, L"..")) return;
    if (MessageBoxW(sf->hwnd, L"确定删除远端该项？不可恢复。", L"YTerminal", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    wchar_t base[1024];
    ssh_base(&sf->s, base, 1024);
    char *un = wide_to_utf8(name);
    wchar_t cmd[2048];
    _snwprintf(cmd, 2048, L"%ls\"rm -rf -- '%hs/%hs'\" 2>&1", base, sf->rcwd, un ? un : "");
    cmd[2047] = 0;
    free(un);
    set_askpass_env(&sf->s);
    char *out = (char *)xcalloc(1, 4096);
    int code = run_capture(cmd, out, 4096, 1);
    set_askpass_env(&sf->s);
    free(out);
    sf_status(sf, code == 0 ? L"已删除远端项" : L"删除失败（可能无权限）");
    sf_remote_refresh(sf);
}

/* ---------------- 双击导航 ---------------- */
static void sf_enter_local(SftpWnd *sf) {
    wchar_t name[300] = { 0 };
    if (!lb_sel_text(sf->list_l, name, 300)) return;
    if (!wcscmp(name, L"..")) {
        path_up(sf->lcwd, L'\\');
    } else {
        size_t len = wcslen(name);
        if (!len || name[len - 1] != L'\\') return;
        name[len - 1] = 0;
        wchar_t next[MAX_PATH * 2];
        _snwprintf(next, MAX_PATH * 2, L"%ls\\%ls", sf->lcwd, name);
        next[MAX_PATH * 2 - 1] = 0;
        wcsncpy(sf->lcwd, next, MAX_PATH * 2 - 1);
        sf->lcwd[MAX_PATH * 2 - 1] = 0;
    }
    sf_local_refresh(sf);
}

static void sf_enter_remote(SftpWnd *sf) {
    wchar_t name[300] = { 0 };
    if (!lb_sel_text(sf->list_r, name, 300)) return;
    if (!wcscmp(name, L"..")) {
        /* 去掉末尾并回退一级 */
        char tmp[512];
        strncpy(tmp, sf->rcwd, 511);
        tmp[511] = 0;
        char *slash = strrchr(tmp, '/');
        if (slash && slash != tmp) *slash = 0;
        else if (slash == tmp) { tmp[1] = 0; }
        strcpy(sf->rcwd, tmp);
        sf_remote_refresh(sf);
        return;
    }
    size_t len = wcslen(name);
    if (!len || name[len - 1] != L'/') { sf_status(sf, L"双击目录进入；文件可下载或删除"); return; }
    name[len - 1] = 0;
    char *n = wide_to_utf8(name);
    char next[512];
    _snprintf(next, 512, "%s/%s", sf->rcwd, n ? n : "");
    next[511] = 0;
    strcpy(sf->rcwd, next);
    free(n);
    sf_remote_refresh(sf);
}

/* ---------------- 布局与绘制 ---------------- */
static void sf_layout(SftpWnd *sf) {
    RECT rc;
    GetClientRect(sf->hwnd, &rc);
    int pad = app_dpi(12);
    int barh = app_dpi(28);
    int btnh = app_dpi(30);
    int btnw = app_dpi(88);
    int gap = app_dpi(12);
    int statusH = app_dpi(24);
    int progH = app_dpi(14);
    int midw = app_dpi(44);
    int colw = (rc.right - pad * 3 - midw) / 2;
    int headh = app_dpi(18);
    int top = pad + headh + barh + gap;
    int listh = rc.bottom - top - statusH - progH - pad * 2 - btnh - gap * 2;
    if (listh < app_dpi(120)) listh = app_dpi(120);
    int lx = pad, rx = pad + colw + midw + pad;

    MoveWindow(GetDlgItem(sf->hwnd, SF_ID_HEAD_L), lx, pad, colw, headh, TRUE);
    MoveWindow(sf->path_l, lx, pad + headh, colw, barh, TRUE);
    MoveWindow(sf->list_l, lx, top, colw, listh, TRUE);
    MoveWindow(GetDlgItem(sf->hwnd, SF_ID_HEAD_R), rx, pad, colw, headh, TRUE);
    MoveWindow(sf->path_r, rx, pad + headh, colw, barh, TRUE);
    MoveWindow(sf->list_r, rx, top, colw, listh, TRUE);
    SendMessageW(sf->list_l, LB_SETHORIZONTALEXTENT, (WPARAM)(colw * 2), 0);
    SendMessageW(sf->list_r, LB_SETHORIZONTALEXTENT, (WPARAM)(colw * 2), 0);

    int by = top + listh + gap;
    int bx = pad;
    int ids[6] = { SF_ID_UP, SF_ID_DOWN, SF_ID_REFRESH, SF_ID_DEL_L, SF_ID_DEL_R, SF_ID_CLOSE };
    for (int i = 0; i < 6; i++) {
        MoveWindow(GetDlgItem(sf->hwnd, ids[i]), bx, by, btnw, btnh, TRUE);
        bx += btnw + gap;
    }

    MoveWindow(sf->progress, pad, by + btnh + gap, rc.right - pad * 2, progH, TRUE);
    MoveWindow(sf->status, pad, rc.bottom - statusH - pad, rc.right - pad * 2, statusH, TRUE);
}

static void sf_paint(SftpWnd *sf) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(sf->hwnd, &ps);
    RECT rc;
    GetClientRect(sf->hwnd, &rc);
    app_fill(hdc, &rc, app_ui_bg());
    RECT top = { 0, 0, rc.right, app_dpi(2) };
    app_fill(hdc, &top, app_ui_accent());
    /* 中间分隔条 */
    int pad = app_dpi(12), midw = app_dpi(44);
    int colw = (rc.right - pad * 3 - midw) / 2;
    RECT mid = { pad + colw + pad / 2, app_dpi(12), pad + colw + pad / 2 + app_dpi(1), rc.bottom - app_dpi(80) };
    RECT hline = { pad, app_dpi(12) + app_dpi(18) + app_dpi(30), rc.right - pad, app_dpi(12) + app_dpi(18) + app_dpi(30) + 1 };
    app_fill(hdc, &hline, app_ui_border());
    app_fill(hdc, &mid, app_ui_border());
    EndPaint(sf->hwnd, &ps);
}

static HWND sf_mkbtn(SftpWnd *sf, int id, const wchar_t *text) {
    HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                             0, 0, 10, 10, sf->hwnd, (HMENU)(INT_PTR)id, g_app.inst, NULL);
    SendMessageW(b, WM_SETFONT, (WPARAM)g_app.f_ui, TRUE);
    return b;
}

static LRESULT CALLBACK SftpProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    SftpWnd *sf = (SftpWnd *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        sf = (SftpWnd *)cs->lpCreateParams;
        sf->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)sf);
        int edh = app_dpi(26);
        HWND hl = CreateWindowExW(0, L"STATIC", L"本地文件", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                  0, 0, 10, 10, h, (HMENU)SF_ID_HEAD_L, g_app.inst, NULL);
        HWND hr = CreateWindowExW(0, L"STATIC", L"远端文件", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                  0, 0, 10, 10, h, (HMENU)SF_ID_HEAD_R, g_app.inst, NULL);
        SendMessageW(hl, WM_SETFONT, (WPARAM)g_app.f_ui, TRUE);
        SendMessageW(hr, WM_SETFONT, (WPARAM)g_app.f_ui, TRUE);
        sf->path_l = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                     0, 0, 10, edh, h, (HMENU)SF_ID_PATH_L, g_app.inst, NULL);
        sf->path_r = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"/", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                     0, 0, 10, edh, h, (HMENU)SF_ID_PATH_R, g_app.inst, NULL);
        SendMessageW(sf->path_l, WM_SETFONT, (WPARAM)g_app.f_ui, TRUE);
        SendMessageW(sf->path_r, WM_SETFONT, (WPARAM)g_app.f_ui, TRUE);

        sf->list_l = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | LBS_DISABLENOSCROLL | LBS_NOINTEGRALHEIGHT |
                                     LBS_NOTIFY | LBS_EXTENDEDSEL,
                                     0, 0, 10, 10, h, (HMENU)SF_ID_LIST_L, g_app.inst, NULL);
        sf->list_r = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | LBS_DISABLENOSCROLL | LBS_NOINTEGRALHEIGHT |
                                     LBS_NOTIFY | LBS_EXTENDEDSEL,
                                     0, 0, 10, 10, h, (HMENU)SF_ID_LIST_R, g_app.inst, NULL);
        SendMessageW(sf->list_l, WM_SETFONT, (WPARAM)g_app.f_latin, TRUE);
        SendMessageW(sf->list_r, WM_SETFONT, (WPARAM)g_app.f_latin, TRUE);

        sf_mkbtn(sf, SF_ID_UP, L"上传 →");
        sf_mkbtn(sf, SF_ID_DOWN, L"← 下载");
        sf_mkbtn(sf, SF_ID_REFRESH, L"刷新");
        sf_mkbtn(sf, SF_ID_DEL_L, L"删除本地");
        sf_mkbtn(sf, SF_ID_DEL_R, L"删除远端");
        sf_mkbtn(sf, SF_ID_CLOSE, L"关闭");

        sf->progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_MARQUEE,
                                       0, 0, 10, 10, h, (HMENU)SF_ID_PROGRESS, g_app.inst, NULL);
        sf->status = CreateWindowExW(0, L"STATIC", L"就绪", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                     0, 0, 10, 10, h, (HMENU)SF_ID_STATUS, g_app.inst, NULL);
        SendMessageW(sf->status, WM_SETFONT, (WPARAM)g_app.f_ui, TRUE);
        ShowWindow(sf->progress, SW_HIDE);
        return 0;
    }
    case WM_SIZE: sf_layout(sf); return 0;
    case WM_PAINT: sf_paint(sf); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, app_ui_dim());
        SetBkColor(dc, app_ui_bg());
        static HBRUSH br = NULL;
        if (!br) br = CreateSolidBrush(app_ui_bg());
        return (LRESULT)br;
    }
    case SF_WM_LIST: {
        sf->listing = 0;
        char *out = (char *)lp;
        if (out) { sf_remote_apply(sf, out, (int)wp); free(out); }
        return 0;
    }
    case SF_WM_JOB: {
        wchar_t st[160];
        _snwprintf(st, 160, L"正在传输 %d/%d 个文件…", (int)wp, (int)lp);
        st[159] = 0;
        sf_status(sf, st);
        return 0;
    }
    case SF_WM_DONE: {
        sf->busy = 0;
        if (sf->thread) { CloseHandle(sf->thread); sf->thread = NULL; }
        SendMessageW(sf->progress, PBM_SETMARQUEE, FALSE, 0);
        ShowWindow(sf->progress, SW_HIDE);
        wchar_t st[160];
        _snwprintf(st, 160, L"传输结束：%d 个文件", sf->job_n);
        st[159] = 0;
        sf_status(sf, st);
        sf->job_n = 0;
        sf_remote_refresh(sf);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == SF_ID_REFRESH) {
            wchar_t wt[MAX_PATH * 2];
            GetDlgItemTextW(h, SF_ID_PATH_L, wt, MAX_PATH * 2);
            if (wt[0]) wcsncpy(sf->lcwd, wt, MAX_PATH * 2 - 1);
            sf->lcwd[MAX_PATH * 2 - 1] = 0;
            GetDlgItemTextW(h, SF_ID_PATH_R, wt, MAX_PATH * 2);
            char *u = wide_to_utf8(wt);
            if (u && *u) { strncpy(sf->rcwd, u, 511); sf->rcwd[511] = 0; }
            free(u);
            sf_local_refresh(sf);
            sf_remote_refresh(sf);
        } else if (id == SF_ID_UP) sf_upload(sf);
        else if (id == SF_ID_DOWN) sf_download(sf);
        else if (id == SF_ID_DEL_L) sf_delete_local(sf);
        else if (id == SF_ID_DEL_R) sf_delete_remote(sf);
        else if (id == SF_ID_CLOSE) DestroyWindow(h);
        else if (id == SF_ID_LIST_L && HIWORD(wp) == LBN_DBLCLK) sf_enter_local(sf);
        else if (id == SF_ID_LIST_R && HIWORD(wp) == LBN_DBLCLK) sf_enter_remote(sf);
        return 0;
    }
    case WM_CLOSE:
        if (sf->busy) { sf_status(sf, L"传输进行中，完成后再关闭"); return 0; }
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        if (g_sftp == sf) g_sftp = NULL;
        if (sf->lthread) { WaitForSingleObject(sf->lthread, 5000); CloseHandle(sf->lthread); sf->lthread = NULL; }
        if (sf->thread) { WaitForSingleObject(sf->thread, 3000); CloseHandle(sf->thread); }
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
    /* 界面自检钩子：无 SSH 会话时也允许开窗（供截图回归脚本使用） */
    int selftest = getenv("YT_UI_SELFTEST") != NULL;
    Session demo;
    ZeroMemory(&demo, sizeof(demo));
    strcpy(demo.user, "demo");
    strcpy(demo.host, "demo");
    demo.protocol = PROTO_SSH;
    if (!s || s->protocol != PROTO_SSH) {
        if (!selftest) {
            MessageBoxW(parent, L"SFTP 仅支持 SSH 会话，请先打开一个 SSH 标签。", L"YTerminal", MB_OK | MB_ICONINFORMATION);
            return;
        }
        s = &demo;
    }
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SftpProc;
    wc.hInstance = g_app.inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"YSftpDual";
    wc.hbrBackground = NULL;
    RegisterClassExW(&wc);

    SftpWnd *sf = (SftpWnd *)xcalloc(1, sizeof(SftpWnd));
    sf->s = *s;
    strcpy(sf->rcwd, "/");
    wcsncpy(sf->lcwd, g_app.set.download_dir, MAX_PATH * 2 - 1);
    if (!sf->lcwd[0]) {
        wchar_t home[MAX_PATH] = { 0 };
        DWORD n = GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
        if (n > 0) wcsncpy(sf->lcwd, home, MAX_PATH * 2 - 1);
    }
    HWND w = CreateWindowExW(0, L"YSftpDual", L"SFTP 文件传输（双栏）", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, app_dpi(1000), app_dpi(620),
                             parent, NULL, g_app.inst, sf);
    if (!w) { free(sf); return; }
    g_sftp = sf;
    ShowWindow(w, SW_SHOW);
    sf_local_refresh(sf);
    sf_remote_refresh(sf);
}
