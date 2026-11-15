#include "app.h"
#include "ui_term.h"
#include <commctrl.h>
#include <objbase.h>
#include <stdio.h>

#ifdef YT_TRACE
#define TRACE(...) do { fprintf(stderr, __VA_ARGS__); fflush(stderr); } while (0)
#else
#define TRACE(...) ((void)0)
#endif

static void open_default_shell(void) {
    Session s = session_defaults(PROTO_LOCAL);
    strcpy(s.name, "本地 Shell");
    if (!s.shell[0]) strcpy(s.shell, "powershell.exe");
    app_open_session(&s, 1);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show) {
    (void)prev; (void)cmdline; (void)show;
    /* 每显示器 DPI 感知（Win10 1703+） */
    typedef BOOL (WINAPI *SetCtxFn)(HANDLE);
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        SetCtxFn f = (SetCtxFn)(void *)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (f) f((HANDLE)-4); /* PER_MONITOR_AWARE_V2 */
        else SetProcessDPIAware();
    }
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES |
                ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    backend_init();

    app_init(inst);
    TRACE("app_init ok\n");
    HWND w = main_window_create(inst);
    TRACE("main_window_create -> %p\n", (void *)w);
    if (!w) return 1;

    app_rebuild_fonts();
    TRACE("fonts ok\n");
    app_apply_theme();
    ShowWindow(w, SW_SHOW);
    UpdateWindow(w);
    TRACE("shown\n");

    app_restore_workspace();
    TRACE("workspace restored, tabs=%d\n", g_app.tab_count);
    if (g_app.tab_count == 0) { open_default_shell(); TRACE("default shell opened, tabs=%d\n", g_app.tab_count); }
    app_layout_content();
    TRACE("layout ok\n");

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(w, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    backend_shutdown();
    CoUninitialize();
    return (int)msg.wParam;
}
