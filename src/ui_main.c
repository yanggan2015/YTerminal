#include "app.h"
#include "ids.h"
#include "ui_term.h"
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* 标签栏                                                              */
/* ================================================================== */
typedef struct {
    HWND hwnd;
    int hot;        /* 悬停标签 */
    int hot_close;
    int pressed;
    RECT rects[128];
    RECT closes[128];
    RECT plus;
    int count;
} TabBar;

static TabBar *tbb_from(HWND h) { return (TabBar *)GetWindowLongPtrW(h, GWLP_USERDATA); }

#define TAB_MIN 96
#define TAB_MAX 240

static void tbb_layout(TabBar *tb, int w) {
    int n = g_app.tab_count;
    tb->count = n;
    if (n <= 0) {
        tb->plus = (RECT){ app_dpi(8), app_dpi(6), app_dpi(8) + app_dpi(28), YT_TAB_H - app_dpi(4) };
        return;
    }
    HDC dc = GetDC(tb->hwnd);
    HFONT old = (HFONT)SelectObject(dc, g_app.f_ui);
    int widths[128];
    int total = app_dpi(8);
    for (int i = 0; i < n; i++) {
        wchar_t title[192] = { 0 };
        app_root_title(i, title, 192);
        int tw = app_text_width(dc, g_app.f_ui, title) + app_dpi(52);
        if (tw < app_dpi(TAB_MIN)) tw = app_dpi(TAB_MIN);
        if (tw > app_dpi(TAB_MAX)) tw = app_dpi(TAB_MAX);
        widths[i] = tw;
        total += tw + app_dpi(2);
    }
    SelectObject(dc, old);
    ReleaseDC(tb->hwnd, dc);

    int avail = w - app_dpi(8) - app_dpi(40);
    if (total > avail && n > 0) {
        int each = (avail - app_dpi(2) * n) / n;
        if (each < app_dpi(60)) each = app_dpi(60);
        for (int i = 0; i < n; i++) widths[i] = each;
    }
    int x = app_dpi(8);
    for (int i = 0; i < n; i++) {
        tb->rects[i] = (RECT){ x, 2, x + widths[i], YT_TAB_H };
        tb->closes[i] = (RECT){ x + widths[i] - app_dpi(22), 2 + (YT_TAB_H - app_dpi(18)) / 2,
                                x + widths[i] - app_dpi(6), 2 + (YT_TAB_H + app_dpi(18)) / 2 };
        x += widths[i] + app_dpi(2);
    }
    tb->plus = (RECT){ x + app_dpi(4), app_dpi(4), x + app_dpi(4) + app_dpi(28), YT_TAB_H - app_dpi(4) };
}

static void tbb_paint(TabBar *tb) {
    RECT rc; GetClientRect(tb->hwnd, &rc);
    HDC hdc = GetDC(tb->hwnd);
    app_fill(hdc, &rc, g_app.theme->ui_panel);
    RECT line = { 0, rc.bottom - 1, rc.right, rc.bottom };
    app_fill(hdc, &line, g_app.theme->ui_border);

    tbb_layout(tb, rc.right);
    for (int i = 0; i < tb->count; i++) {
        int active = (i == g_app.active_tab);
        RECT tr = tb->rects[i];
        COLORREF bg = active ? g_app.theme->bg : g_app.theme->ui_panel;
        if (i == tb->hot && !active) bg = g_app.theme->ui_hover;
        app_fill(hdc, &tr, bg);
        if (active) {
            RECT acc = { tr.left, tr.top, tr.right, tr.top + app_dpi(2) };
            app_fill(hdc, &acc, g_app.theme->ui_accent);
        }
        wchar_t title[192] = { 0 };
        app_root_title(i, title, 192);
        RECT txt = tr;
        txt.left += app_dpi(12);
        txt.right -= app_dpi(26);
        app_draw_text(hdc, title, &txt, g_app.f_ui,
                      active ? g_app.theme->ui_text : g_app.theme->ui_dim,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        /* 关闭按钮 */
        RECT cr = tb->closes[i];
        if (i == tb->hot && tb->hot_close) {
            app_fill_round(hdc, &cr, app_dpi(3), g_app.theme->ui_border);
        }
        RECT xr = cr;
        app_draw_text(hdc, L"×", &xr, g_app.f_ui_b,
                      (i == tb->hot && tb->hot_close) ? g_app.theme->ui_text : g_app.theme->ui_dim,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    /* + 按钮 */
    RECT pr = tb->plus;
    app_fill_round(hdc, &pr, app_dpi(4), tb->hot == -2 ? g_app.theme->ui_hover : g_app.theme->ui_panel);
    app_draw_text(hdc, L"+", &pr, g_app.f_ui_b, g_app.theme->ui_text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    ReleaseDC(tb->hwnd, hdc);
}

static int tbb_hit(TabBar *tb, int x, int y, int *on_close) {
    *on_close = 0;
    if (PtInRect(&tb->plus, (POINT){ x, y })) return -2;
    for (int i = 0; i < tb->count; i++) {
        if (PtInRect(&tb->rects[i], (POINT){ x, y })) {
            if (PtInRect(&tb->closes[i], (POINT){ x, y })) *on_close = 1;
            return i;
        }
    }
    return -1;
}

static LRESULT CALLBACK TabBarProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    TabBar *tb = tbb_from(h);
    switch (msg) {
    case WM_CREATE:
        tb = (TabBar *)xcalloc(1, sizeof(TabBar));
        tb->hwnd = h; tb->hot = -1; tb->hot_close = 0;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)tb);
        return 0;
    case WM_DESTROY: free(tb); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); tbb_paint(tb); EndPaint(h, &ps); return 0; }
    case WM_SIZE: tbb_layout(tb, LOWORD(lp)); InvalidateRect(h, NULL, FALSE); return 0;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int close = 0;
        int hot = tbb_hit(tb, x, y, &close);
        if (hot != tb->hot || close != tb->hot_close) {
            tb->hot = hot; tb->hot_close = close;
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE: tb->hot = -1; tb->hot_close = 0; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int close = 0;
        int hit = tbb_hit(tb, x, y, &close);
        if (hit == -2) { PostMessageW(g_app.main, WM_COMMAND, IDC_NEW_LOCAL, 0); return 0; }
        if (hit >= 0) {
            if (close) {
                if (g_app.set.confirm_close) {
                    wchar_t title[192]; app_root_title(hit, title, 192);
                    wchar_t msg[300];
                    _snwprintf(msg, 300, L"关闭标签「%s」？", title);
                    if (MessageBoxW(g_app.main, msg, L"YTerminal", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
                }
                PostMessageW(g_app.main, WM_COMMAND, IDC_CLOSE_TAB + 1000 * (hit + 1), 0);
                return 0;
            }
            app_activate_tab(hit);
        }
        return 0;
    }
    case WM_MBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int close = 0;
        int hit = tbb_hit(tb, x, y, &close);
        if (hit >= 0) PostMessageW(g_app.main, WM_COMMAND, IDC_CLOSE_TAB + 1000 * (hit + 1), 0);
        return 0;
    }
    case WM_RBUTTONUP: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int close = 0;
        int hit = tbb_hit(tb, x, y, &close);
        if (hit >= 0) {
            app_activate_tab(hit);
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, 1, L"重命名标签…");
            AppendMenuW(m, MF_STRING, 2, L"复制会话");
            AppendMenuW(m, MF_STRING, 3, L"重连");
            AppendMenuW(m, MF_STRING, 7, L"复制标签标题");
            AppendMenuW(m, MF_STRING, 8, L"命令面板…");
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            AppendMenuW(m, MF_STRING, 4, L"关闭标签");
            AppendMenuW(m, MF_STRING, 5, L"关闭其他标签");
            AppendMenuW(m, MF_STRING, 6, L"关闭右侧标签");
            POINT pt; GetCursorPos(&pt);
            int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN, pt.x, pt.y, 0, h, NULL);
            DestroyMenu(m);
            if (cmd == 1) PostMessageW(g_app.main, WM_COMMAND, IDC_RENAME_TAB, 0);
            else if (cmd == 2) PostMessageW(g_app.main, WM_COMMAND, IDC_DUPLICATE, 0);
            else if (cmd == 3) PostMessageW(g_app.main, WM_COMMAND, IDC_RECONNECT, 0);
            else if (cmd == 4) PostMessageW(g_app.main, WM_COMMAND, IDC_CLOSE_TAB, 0);
            else if (cmd == 5) PostMessageW(g_app.main, WM_COMMAND, IDC_CLOSE_OTHERS, 0);
            else if (cmd == 6) PostMessageW(g_app.main, WM_COMMAND, IDC_CLOSE_RIGHT, 0);
            else if (cmd == 7) PostMessageW(g_app.main, WM_COMMAND, IDC_COPY_TAB_TITLE, 0);
            else if (cmd == 8) PostMessageW(g_app.main, WM_COMMAND, IDC_PALETTE, 0);
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int close = 0;
        if (tbb_hit(tb, x, y, &close) == -1) PostMessageW(g_app.main, WM_COMMAND, IDC_QUICK_CONNECT, 0);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

HWND tabbar_create(HWND parent, HINSTANCE inst) {
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = TabBarProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"YTabBar";
    RegisterClassExW(&wc);
    return CreateWindowExW(0, L"YTabBar", L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, YT_TAB_H, parent, NULL, inst, NULL);
}
void tabbar_resize(HWND h, RECT *rc) { MoveWindow(h, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, TRUE); }
void tabbar_refresh(HWND h) { InvalidateRect(h, NULL, FALSE); }

/* ================================================================== */
/* 主窗口                                                              */
/* ================================================================== */
static LRESULT CALLBACK MainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_app.main = h;
        g_app.toolbar = toolbar_create(h, g_app.inst);
        g_app.tabbar = tabbar_create(h, g_app.inst);
        g_app.sidebar = sidebar_create(h, g_app.inst);
        g_app.statusbar = statusbar_create(h, g_app.inst);
        SetTimer(h, 1, 1000, NULL);
        app_layout_content();
        return 0;
    case WM_SIZE:
        app_layout_content();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = app_dpi(900);
        mm->ptMinTrackSize.y = app_dpi(560);
        return 0;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wp;
        RECT rc; GetClientRect(h, &rc);
        app_fill(hdc, &rc, g_app.theme->ui_bg);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        app_fill(hdc, &rc, g_app.theme->ui_bg);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_COMMAND: {
        UINT id = LOWORD(wp);
        if (id >= IDC_CLOSE_TAB + 1000) {
            int tab = (int)(id - IDC_CLOSE_TAB) / 1000 - 1;
            app_close_tab(tab);
            return 0;
        }
        app_command(id);
        return 0;
    }
    case WM_TIMER:
        if (wp == 1) {
            if (g_app.statusbar) statusbar_update(g_app.statusbar);
        }
        return 0;
    case WM_YT_TITLE_CHANGED:
        app_update_tab_title((HWND)wp);
        return 0;
    case WM_YT_VIEW_STATUS:
        if (g_app.statusbar) statusbar_update(g_app.statusbar);
        if (g_app.tabbar) tabbar_refresh(g_app.tabbar);
        return 0;
    case WM_YT_SIDEBAR_REFRESH:
        if (g_app.sidebar) sidebar_refresh(g_app.sidebar);
        return 0;
    case WM_YT_APPLY_SETTINGS: {
        for (int i = 0; i < g_app.tab_count; i++) {
            HWND v = app_root_view(g_app.roots[i]);
            if (v) SendMessageW(v, WM_YT_APPLY_SETTINGS, 0, 0);
        }
        app_layout_content();
        if (g_app.toolbar) InvalidateRect(g_app.toolbar, NULL, FALSE);
        if (g_app.tabbar) InvalidateRect(g_app.tabbar, NULL, FALSE);
        if (g_app.sidebar) InvalidateRect(g_app.sidebar, NULL, FALSE);
        return 0;
    }
    case WM_SETFOCUS: {
        HWND v = app_active_view();
        if (v) SetFocus(v);
        return 0;
    }
    case WM_CLOSE:
        if (g_app.closing) return 0;
        g_app.closing = 1;
        app_shutdown();
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

HWND main_window_create(HINSTANCE inst) {
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hIcon = LoadIconW(inst, L"APPICON");
    wc.lpszClassName = L"YTerminalMain";
    wc.hbrBackground = NULL;
    if (!RegisterClassExW(&wc)) return NULL;

    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int avail_w = wa.right - wa.left, avail_h = wa.bottom - wa.top;
    int w = (int)(avail_w * 0.86), h = (int)(avail_h * 0.88);
    if (w < 1000) w = 1000;
    if (h < 640) h = 640;
    if (w > avail_w) w = avail_w;
    if (h > avail_h) h = avail_h;
    HWND mw = CreateWindowExW(0, L"YTerminalMain", L"YTerminal — 全能终端", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              wa.left + (avail_w - w) / 2, wa.top + (avail_h - h) / 2, w, h, NULL, NULL, inst, NULL);
    return mw;
}
