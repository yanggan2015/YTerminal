#include "yt_core.h"

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif
#ifndef HPCON
typedef VOID *HPCON;
#endif

typedef HRESULT (WINAPI *FnCreatePC)(COORD, HANDLE, HANDLE, DWORD, HPCON *);
typedef void (WINAPI *FnClosePC)(HPCON);
typedef HRESULT (WINAPI *FnResizePC)(HPCON, COORD);

typedef struct {
    HPCON hpc;
    HANDLE in_wr, out_rd, proc, reader;
    FnClosePC close_pc;
    FnResizePC resize_pc;
    volatile LONG run;
    YtSession *owner;
} PtyImpl;

typedef struct {
    HPCON hpc;
    HANDLE proc;
    FnClosePC close_pc;
} PtyLateClose;

static DWORD WINAPI reader_thr(LPVOID arg)
{
    PtyImpl *p = (PtyImpl *)arg;
    char buf[8192];
    DWORD rd;
    while (InterlockedCompareExchange(&p->run, 1, 1) == 1) {
        HANDLE out = p->out_rd;
        if (!out) break;
        if (!ReadFile(out, buf, sizeof(buf), &rd, NULL)) {
            DWORD e = GetLastError();
            if (e == ERROR_BROKEN_PIPE || e == ERROR_INVALID_HANDLE ||
                e == ERROR_OPERATION_ABORTED)
                break;
            Sleep(5);
            continue;
        }
        if (rd == 0) {
            if (p->proc && WaitForSingleObject(p->proc, 0) == WAIT_OBJECT_0) break;
            Sleep(1);
            continue;
        }
        {
            YtSession *s = p->owner;
            if (s && s->hwnd_term) yt_session_post_out(s, buf, (int)rd);
        }
    }
    {
        YtSession *s = p->owner;
        if (s && s->hwnd_main)
            PostMessageW(s->hwnd_main, WM_YT_CLOSED, (WPARAM)s->tab_index, 0);
    }
    return 0;
}

static void pty_write(YtSession *s, const char *data, int len)
{
    PtyImpl *p = (PtyImpl *)s->impl;
    DWORD wr;
    int off = 0;
    if (!p || !p->in_wr || len <= 0) return;
    while (off < len) {
        if (!WriteFile(p->in_wr, data + off, (DWORD)(len - off), &wr, NULL) || wr == 0) break;
        off += (int)wr;
    }
}

static void pty_resize(YtSession *s, int cols, int rows)
{
    PtyImpl *p = (PtyImpl *)s->impl;
    COORD sz = { (SHORT)(cols > 0 ? cols : YT_COLS), (SHORT)(rows > 0 ? rows : YT_ROWS) };
    if (p && p->hpc && p->resize_pc) p->resize_pc(p->hpc, sz);
}

static DWORD WINAPI pty_late_close_thr(LPVOID arg)
{
    PtyLateClose *j = (PtyLateClose *)arg;
    if (j->proc) {
        WaitForSingleObject(j->proc, 300);
        CloseHandle(j->proc);
    }
    if (j->hpc && j->close_pc) j->close_pc(j->hpc);
    free(j);
    return 0;
}

static void pty_close(YtSession *s)
{
    PtyImpl *p = (PtyImpl *)s->impl;
    HANDLE out, in_wr, reader;
    PtyLateClose *j;
    if (!p) return;
    s->impl = NULL;
    s->write_fn = NULL;
    s->resize_fn = NULL;
    s->close_fn = NULL;
    p->owner = NULL;
    InterlockedExchange(&p->run, 0);

    in_wr = p->in_wr; p->in_wr = NULL;
    out = p->out_rd; p->out_rd = NULL;
    if (in_wr) CloseHandle(in_wr);
    if (out) {
        CancelIoEx(out, NULL);
        CloseHandle(out);
    }
    if (p->proc) TerminateProcess(p->proc, 0);

    reader = p->reader; p->reader = NULL;
    if (reader) {
        WaitForSingleObject(reader, 800); /* 管道已关，应很快退出；限时防卡死 */
        CloseHandle(reader);
    }

    j = (PtyLateClose *)calloc(1, sizeof(PtyLateClose));
    if (j) {
        j->hpc = p->hpc;
        j->proc = p->proc;
        j->close_pc = p->close_pc;
        p->hpc = NULL;
        p->proc = NULL;
        CreateThread(NULL, 0, pty_late_close_thr, j, 0, NULL);
    } else {
        if (p->proc) { CloseHandle(p->proc); p->proc = NULL; }
        if (p->hpc && p->close_pc) p->close_pc(p->hpc);
        p->hpc = NULL;
    }
    free(p);
}

static BOOL resolve_shell(const wchar_t *shell, wchar_t *cmd, int cap)
{
    wchar_t path[MAX_PATH];
    if (_wcsicmp(shell, L"cmd") == 0 || _wcsicmp(shell, L"cmd.exe") == 0) {
        if (GetSystemDirectoryW(path, MAX_PATH)) {
            swprintf(cmd, cap, L"%ls\\cmd.exe", path);
            return TRUE;
        }
        wcsncpy(cmd, L"cmd.exe", cap - 1);
        return TRUE;
    }
    if (SearchPathW(NULL, L"powershell.exe", NULL, MAX_PATH, path, NULL)) {
        swprintf(cmd, cap, L"\"%ls\" -NoLogo -NoProfile", path);
        return TRUE;
    }
    {
        wchar_t windir[MAX_PATH];
        if (GetWindowsDirectoryW(windir, MAX_PATH)) {
            swprintf(cmd, cap,
                     L"\"%ls\\System32\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoLogo -NoProfile",
                     windir);
            return TRUE;
        }
    }
    wcsncpy(cmd, L"powershell.exe -NoLogo -NoProfile", cap - 1);
    return TRUE;
}

BOOL yt_local_start(YtSession *s)
{
    FnCreatePC create_pc;
    FnClosePC close_pc;
    FnResizePC resize_pc;
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    HANDLE pty_in_rd = NULL, out_wr = NULL;
    HANDLE in_wr = NULL, out_rd = NULL;
    STARTUPINFOEXW si;
    PROCESS_INFORMATION pi;
    SIZE_T attr = 0;
    COORD size = { YT_COLS, YT_ROWS };
    PtyImpl *p;
    wchar_t cmd[YT_PATH_MAX];
    HRESULT hr;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };

    create_pc = (FnCreatePC)GetProcAddress(k, "CreatePseudoConsole");
    close_pc = (FnClosePC)GetProcAddress(k, "ClosePseudoConsole");
    resize_pc = (FnResizePC)GetProcAddress(k, "ResizePseudoConsole");
    if (!create_pc) {
        strncpy(s->err, "ConPTY unavailable (need Win10 1809+)", sizeof(s->err) - 1);
        return FALSE;
    }

    resolve_shell(s->shell[0] ? s->shell : L"powershell", cmd, YT_PATH_MAX);

    if (!CreatePipe(&pty_in_rd, &in_wr, &sa, 256 * 1024) ||
        !CreatePipe(&out_rd, &out_wr, &sa, 256 * 1024)) {
        strncpy(s->err, "CreatePipe failed", sizeof(s->err) - 1);
        return FALSE;
    }
    SetHandleInformation(in_wr, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);

    p = (PtyImpl *)calloc(1, sizeof(PtyImpl));
    p->close_pc = close_pc;
    p->resize_pc = resize_pc;
    p->owner = s;
    hr = create_pc(size, pty_in_rd, out_wr, 0, &p->hpc);
    CloseHandle(pty_in_rd);
    CloseHandle(out_wr);
    if (FAILED(hr)) {
        snprintf(s->err, sizeof(s->err), "CreatePseudoConsole hr=0x%08lX", (unsigned long)hr);
        CloseHandle(in_wr); CloseHandle(out_rd); free(p);
        return FALSE;
    }
    p->in_wr = in_wr;
    p->out_rd = out_rd;

    ZeroMemory(&si, sizeof(si));
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr);
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attr);
    if (!si.lpAttributeList ||
        !InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr) ||
        !UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   p->hpc, sizeof(HPCON), NULL, NULL)) {
        strncpy(s->err, "ProcThreadAttribute failed", sizeof(s->err) - 1);
        close_pc(p->hpc); CloseHandle(in_wr); CloseHandle(out_rd);
        if (si.lpAttributeList) HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
        free(p);
        return FALSE;
    }

    ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE,
                        EXTENDED_STARTUPINFO_PRESENT,
                        NULL, NULL, &si.StartupInfo, &pi)) {
        snprintf(s->err, sizeof(s->err), "CreateProcess %lu cmd=%ls", GetLastError(), cmd);
        DeleteProcThreadAttributeList(si.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
        close_pc(p->hpc); CloseHandle(in_wr); CloseHandle(out_rd); free(p);
        return FALSE;
    }
    DeleteProcThreadAttributeList(si.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
    CloseHandle(pi.hThread);

    p->proc = pi.hProcess;
    InterlockedExchange(&p->run, 1);
    s->impl = p;
    s->kind = YT_KIND_LOCAL;
    s->write_fn = pty_write;
    s->resize_fn = pty_resize;
    s->close_fn = pty_close;
    s->state = YT_ST_CONNECTED;
    swprintf(s->title, YT_TITLE_MAX, L"Local:%ls", s->shell[0] ? s->shell : L"powershell");
    if (!s->name[0]) wcscpy(s->name, s->title);

    p->reader = CreateThread(NULL, 0, reader_thr, p, 0, NULL);
    if (resize_pc) resize_pc(p->hpc, size);
    return TRUE;
}
