#include "yt_core.h"

/* 仅保留 X11 旁载；RDP/VNC 已移除，远程字符会话见 telnet_session.c / ssh_session.c */

BOOL yt_x11_ensure(void)
{
    wchar_t xsrv[YT_PATH_MAX];
    wchar_t cmd[YT_PATH_MAX * 2];
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    if (FindWindowW(L"VcXsrv/x", NULL)) return TRUE;
    swprintf(xsrv, YT_PATH_MAX, L"%ls\\x11\\vcxsrv.exe", yt_paths()->runtime);
    if (GetFileAttributesW(xsrv) == INVALID_FILE_ATTRIBUTES) {
        swprintf(xsrv, YT_PATH_MAX, L"%ls\\x11\\Xming.exe", yt_paths()->runtime);
        if (GetFileAttributesW(xsrv) == INVALID_FILE_ATTRIBUTES) return FALSE;
    }
    swprintf(cmd, YT_PATH_MAX * 2, L"\"%ls\" :0 -multiwindow -clipboard -wgl", xsrv);
    si.cb = sizeof(si);
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return FALSE;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    SetEnvironmentVariableW(L"DISPLAY", L"127.0.0.1:0");
    return TRUE;
}
