#include "app.h"
#include <shellapi.h>

void yt_app_fill_from_ui(YtSession *s)
{
    int k = yt_app_kind_sel();
    GetWindowTextW(g.host, s->host, YT_HOST_MAX);
    { wchar_t b[16]; GetWindowTextW(g.port, b, 16); s->port = _wtoi(b); }
    GetWindowTextW(g.user, s->user, 64);
    GetWindowTextW(g.pass, s->pass, 128);
    GetWindowTextW(g.com, s->com, 32);
    { wchar_t b[16]; GetWindowTextW(g.baud, b, 16); s->baud = _wtoi(b); }
    {
        int si = (int)SendMessageW(g.shell, CB_GETCURSEL, 0, 0);
        wcscpy(s->shell, si == 1 ? L"cmd" : L"powershell");
    }
    GetWindowTextW(g.jump, s->jump_host, YT_HOST_MAX);
    GetWindowTextW(g.key, s->keyfile, YT_PATH_MAX);
    {
        wchar_t fw[512]; char f8[512];
        GetWindowTextW(g.fwd, fw, 512);
        WideCharToMultiByte(CP_UTF8, 0, fw, -1, f8, sizeof(f8), NULL, NULL);
        strncpy(s->forwards, f8, sizeof(s->forwards) - 1);
    }
    if (k == 0) s->kind = YT_KIND_LOCAL;
    else if (k == 1) s->kind = YT_KIND_SSH;
    else if (k == 2) s->kind = YT_KIND_SERIAL;
    else s->kind = YT_KIND_TELNET;
}

void yt_app_do_connect(void)
{
    YtSession *s = (YtSession *)calloc(1, sizeof(YtSession));
    yt_session_init(s);
    yt_app_fill_from_ui(s);
    if ((s->kind == YT_KIND_SSH || s->kind == YT_KIND_TELNET) && !s->host[0]) {
        yt_app_set_status(L"请填写主机");
        yt_session_unref(s);
        return;
    }
    if (s->kind == YT_KIND_SERIAL && !s->com[0]) {
        yt_app_set_status(L"请填写 COM 口");
        yt_session_unref(s);
        return;
    }
    if (!s->port) {
        if (s->kind == YT_KIND_SSH) s->port = 22;
        else if (s->kind == YT_KIND_TELNET) s->port = 23;
    }
    if (!s->baud) s->baud = 115200;
    if (s->kind == YT_KIND_SSH && s->jump_host[0]) {
        wchar_t *at = wcschr(s->jump_host, L'@');
        if (at) {
            *at = 0;
            wcscpy(s->jump_user, s->jump_host);
            wcscpy(s->jump_host, at + 1);
        }
    }
    if (s->kind == YT_KIND_SSH) {
        swprintf(s->title, YT_TITLE_MAX, L"%ls@%ls:%d", s->user, s->host, s->port ? s->port : 22);
        yt_app_add_tab(s);
        yt_app_set_status(L"SSH 连接中...");
        if (!yt_ssh_start_async(g.hwnd, s)) {
            yt_app_set_status(L"SSH 启动失败");
            yt_app_close_tab(g.ntabs - 1);
        }
        return;
    }
    if (s->kind == YT_KIND_LOCAL) {
        swprintf(s->title, YT_TITLE_MAX, L"Local:%ls", s->shell[0] ? s->shell : L"powershell");
        yt_app_add_tab(s);
        if (!yt_local_start(s)) yt_app_start_ok(s, FALSE);
        else yt_app_set_status(L"已连接");
    } else if (s->kind == YT_KIND_SERIAL) {
        yt_app_add_tab(s);
        if (!yt_serial_start(s)) yt_app_start_ok(s, FALSE);
        else yt_app_set_status(L"已连接");
    } else if (s->kind == YT_KIND_TELNET) {
        swprintf(s->title, YT_TITLE_MAX, L"Telnet:%ls:%d", s->host, s->port ? s->port : 23);
        yt_app_add_tab(s);
        if (!yt_telnet_start(s)) yt_app_start_ok(s, FALSE);
        else yt_app_set_status(L"已连接");
    }
}

void yt_app_start_local(const wchar_t *shell, BOOL admin)
{
    YtSession *s;
    wchar_t sh[64];
    if (admin) {
        wchar_t path[MAX_PATH];
        if (SearchPathW(NULL, L"powershell.exe", NULL, MAX_PATH, path, NULL))
            ShellExecuteW(g.hwnd, L"runas", path, L"-NoLogo", NULL, SW_SHOWNORMAL);
        else
            yt_app_set_status(L"未找到 powershell");
        return;
    }
    /* 工具栏「本地」优先读连接栏 Shell 下拉 */
    if (!shell || !shell[0]) {
        int si = (int)SendMessageW(g.shell, CB_GETCURSEL, 0, 0);
        wcscpy(sh, si == 1 ? L"cmd" : L"powershell");
        shell = sh;
    }
    s = (YtSession *)calloc(1, sizeof(YtSession));
    yt_session_init(s);
    s->kind = YT_KIND_LOCAL;
    wcsncpy(s->shell, shell, 63);
    swprintf(s->title, YT_TITLE_MAX, L"Local:%ls", s->shell);
    yt_app_add_tab(s);
    if (!yt_local_start(s)) yt_app_start_ok(s, FALSE);
    else yt_app_set_status(L"已连接");
}

void yt_app_clone_cur(void)
{
    int cur = yt_app_cur_tab();
    YtSession *s, *src;
    if (cur < 0) return;
    src = g.sess[cur];
    s = (YtSession *)calloc(1, sizeof(YtSession));
    yt_app_profile_from_session(s, src);
    wcsncpy(s->pass, src->pass, 127);
    wcsncpy(s->title, src->title, YT_TITLE_MAX - 1);
    if (s->kind == YT_KIND_SSH) {
        yt_app_add_tab(s);
        if (!yt_ssh_start_async(g.hwnd, s)) yt_app_close_tab(g.ntabs - 1);
    } else if (s->kind == YT_KIND_SERIAL) {
        yt_app_add_tab(s);
        if (!yt_serial_start(s)) yt_app_start_ok(s, FALSE); else yt_app_set_status(L"已连接");
    } else if (s->kind == YT_KIND_TELNET) {
        yt_app_add_tab(s);
        if (!yt_telnet_start(s)) yt_app_start_ok(s, FALSE); else yt_app_set_status(L"已连接");
    } else if (s->kind == YT_KIND_LOCAL) {
        yt_app_add_tab(s);
        if (!yt_local_start(s)) yt_app_start_ok(s, FALSE); else yt_app_set_status(L"已连接");
    } else {
        yt_session_unref(s);
        yt_app_set_status(L"无法克隆该类型");
    }
}

void yt_app_reconnect_cur(void)
{
    int cur = yt_app_cur_tab();
    YtSession tmp, *s;
    if (cur < 0) return;
    yt_app_profile_from_session(&tmp, g.sess[cur]);
    wcsncpy(tmp.pass, g.sess[cur]->pass, 127);
    wcsncpy(tmp.title, g.sess[cur]->title, YT_TITLE_MAX - 1);
    if (tmp.kind == YT_KIND_LOCAL) {
        yt_app_set_status(L"本地会话无需重连，请新建");
        DeleteCriticalSection(&tmp.lock);
        return;
    }
    yt_app_close_tab(cur);
    s = (YtSession *)calloc(1, sizeof(YtSession));
    yt_app_profile_from_session(s, &tmp);
    wcsncpy(s->pass, tmp.pass, 127);
    wcsncpy(s->title, tmp.title, YT_TITLE_MAX - 1);
    DeleteCriticalSection(&tmp.lock);
    if (s->kind == YT_KIND_SSH) {
        yt_app_add_tab(s);
        if (!yt_ssh_start_async(g.hwnd, s)) yt_app_close_tab(g.ntabs - 1);
    } else if (s->kind == YT_KIND_SERIAL) {
        yt_app_add_tab(s);
        if (!yt_serial_start(s)) yt_app_start_ok(s, FALSE); else yt_app_set_status(L"已连接");
    } else if (s->kind == YT_KIND_TELNET) {
        yt_app_add_tab(s);
        if (!yt_telnet_start(s)) yt_app_start_ok(s, FALSE); else yt_app_set_status(L"已连接");
    } else {
        yt_session_unref(s);
    }
}
