#include "app.h"
#include "ids.h"
#include "ui_term.h"
#include <windowsx.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* 工具栏                                                              */
/* ================================================================== */
typedef struct { int id; const wchar_t *label; int group; } ToolBtn;
static const ToolBtn g_btns[] = {
    { IDC_NEW_SSH,      L"新建会话", 1 },
    { IDC_QUICK_CONNECT,L"快速连接", 0 },
    { IDC_DUPLICATE,    L"复制会话", 0 },
    { IDC_SFTP,         L"SFTP",     1 },
    { IDC_SPLIT_V,      L"左右分屏", 0 },
    { IDC_SPLIT_H,      L"上下分屏", 0 },
    { IDC_BROADCAST,    L"广播",     1 },
    { IDC_FIND,         L"查找",     0 },
    { IDC_LOG,          L"日志",     0 },
    { IDC_RECONNECT,    L"重连",     0 },
    { IDC_SETTINGS,     L"设置",     1 },
};
#define BTN_COUNT ((int)(sizeof(g_btns) / sizeof(g_btns[0])))

typedef struct {
    HWND hwnd;
    int hot;
    int pressed;
    RECT rects[BTN_COUNT];
} Toolbar;

static Toolbar *tb_from(HWND h) { return (Toolbar *)GetWindowLongPtrW(h, GWLP_USERDATA); }

static void tb_layout(Toolbar *tb, int w) {
    int x = app_dpi(10);
    int h = app_dpi(28);
    int y = app_dpi((YT_TOOLBAR_H - 28) / 2);
    HDC dc = GetDC(tb->hwnd);
    HFONT old = (HFONT)SelectObject(dc, g_app.f_ui);
    for (int i = 0; i < BTN_COUNT; i++) {
        if (g_btns[i].group && i) x += app_dpi(9);
        int tw = app_text_width(dc, g_app.f_ui, (wchar_t *)g_btns[i].label) + app_dpi(22);
        if (tw < app_dpi(56)) tw = app_dpi(56);
        tb->rects[i].left = x;
        tb->rects[i].top = y;
        tb->rects[i].right = x + tw;
        tb->rects[i].bottom = y + h;
        x += tw + app_dpi(4);
    }
    SelectObject(dc, old);
    ReleaseDC(tb->hwnd, dc);
}

static void tb_paint(Toolbar *tb) {
    RECT rc;
    GetClientRect(tb->hwnd, &rc);
    HDC hdc = GetDC(tb->hwnd);
    app_fill(hdc, &rc, g_app.theme->ui_panel);
    RECT line = { 0, rc.bottom - 1, rc.right, rc.bottom };
    app_fill(hdc, &line, g_app.theme->ui_border);

    tb_layout(tb, rc.right);
    for (int i = 0; i < BTN_COUNT; i++) {
        int active = 0;
        if (g_btns[i].id == IDC_BROADCAST) active = g_app.broadcast;
        if (g_btns[i].id == IDC_LOG) {
            HWND v = app_active_view();
            active = v ? term_view_is_logging(v) : 0;
        }
        RECT r = tb->rects[i];
        COLORREF bg = g_app.theme->ui_panel;
        if (active) bg = g_app.theme->ui_accent;
        else if (i == tb->hot) bg = g_app.theme->ui_hover;
        if (i == tb->pressed) bg = g_app.theme->ui_border;
        if (bg != g_app.theme->ui_panel) app_fill_round(hdc, &r, app_dpi(4), bg);
        COLORREF fg = active ? g_app.theme->ui_text : g_app.theme->ui_dim;
        if (i == tb->hot && !active) fg = g_app.theme->ui_text;
        app_draw_text(hdc, (wchar_t *)g_btns[i].label, &r, g_app.f_ui, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    ReleaseDC(tb->hwnd, hdc);
}

static LRESULT CALLBACK ToolbarProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Toolbar *tb = tb_from(h);
    switch (msg) {
    case WM_CREATE:
        tb = (Toolbar *)xcalloc(1, sizeof(Toolbar));
        tb->hwnd = h;
        tb->hot = -1; tb->pressed = -1;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)tb);
        return 0;
    case WM_DESTROY: free(tb); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); tb_paint(tb); EndPaint(h, &ps); return 0; }
    case WM_SIZE: tb_layout(tb, LOWORD(lp)); InvalidateRect(h, NULL, FALSE); return 0;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int hot = -1;
        static int tracking = 0;
        if (!tracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
            tracking = 1;
        }
        for (int i = 0; i < BTN_COUNT; i++)
            if (PtInRect(&tb->rects[i], (POINT){ x, y })) hot = i;
        if (hot != tb->hot) {
            tb->hot = hot;
            InvalidateRect(h, NULL, FALSE);
            if (hot >= 0) app_status_set((wchar_t *)g_btns[hot].label);
        }
        return 0;
    }
    case WM_MOUSELEAVE: tb->hot = -1; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        tb->pressed = -1;
        for (int i = 0; i < BTN_COUNT; i++) if (PtInRect(&tb->rects[i], (POINT){ x, y })) { tb->pressed = i; break; }
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int cmd = -1;
        for (int i = 0; i < BTN_COUNT; i++) if (PtInRect(&tb->rects[i], (POINT){ x, y }) && i == tb->pressed) cmd = g_btns[i].id;
        tb->pressed = -1;
        InvalidateRect(h, NULL, FALSE);
        if (cmd > 0) PostMessageW(g_app.main, WM_COMMAND, cmd, 0);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

HWND toolbar_create(HWND parent, HINSTANCE inst) {
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ToolbarProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"YToolbar";
    RegisterClassExW(&wc);
    return CreateWindowExW(0, L"YToolbar", L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, YT_TOOLBAR_H, parent, NULL, inst, NULL);
}
void toolbar_resize(HWND h, RECT *rc) { MoveWindow(h, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, TRUE); }

/* ================================================================== */
/* 侧边栏                                                              */
/* ================================================================== */
typedef struct {
    HWND hwnd, search;
    int hot, selected, scroll;
    int count;
    int *map;      /* 行 -> 会话索引 */
    int header_y;
} Sidebar;

static Sidebar *sb_from(HWND h) { return (Sidebar *)GetWindowLongPtrW(h, GWLP_USERDATA); }
static WNDPROC g_sb_edit_old = NULL;

#define SIDE_ROW_H 48
#define SIDE_HEAD_H 26

static void sb_filter(Sidebar *sb) {
    free(sb->map);
    sb->map = (int *)xcalloc(g_app.sessions.n + 1, sizeof(int));
    sb->count = 0;
    wchar_t wt[128] = { 0 };
    if (sb->search) GetWindowTextW(sb->search, wt, 128);
    char *q = wide_to_utf8(wt);
    for (int i = 0; i < g_app.sessions.n; i++) {
        Session *s = &g_app.sessions.items[i];
        if (q && *q) {
            char sub[256];
            session_subtitle(s, sub, 256);
            if (!yt_contains_ci(s->name, q) && !yt_contains_ci(sub, q) && !yt_contains_ci(s->group, q)) continue;
        }
        sb->map[sb->count++] = i;
    }
    free(q);
    if (sb->selected >= sb->count) sb->selected = sb->count - 1;
    if (sb->selected < 0) sb->selected = 0;
}

static void sb_paint(Sidebar *sb) {
    RECT rc;
    GetClientRect(sb->hwnd, &rc);
    HDC hdc = GetDC(sb->hwnd);
    app_fill(hdc, &rc, g_app.theme->ui_panel);
    RECT edge = { rc.right - 1, 0, rc.right, rc.bottom };
    app_fill(hdc, &edge, g_app.theme->ui_border);

    /* 按钮行 */
    int by = app_dpi(8);
    RECT br = { app_dpi(10), by, app_dpi(10) + app_dpi(64), by + app_dpi(26) };
    app_fill_round(hdc, &br, app_dpi(4), g_app.theme->ui_hover);
    app_draw_text(hdc, L"+ 新建", &br, g_app.f_ui, g_app.theme->ui_text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT bi = br; bi.left = br.right + app_dpi(6); bi.right = bi.left + app_dpi(64);
    app_fill_round(hdc, &bi, app_dpi(4), g_app.theme->ui_hover);
    app_draw_text(hdc, L"导入", &bi, g_app.f_ui, g_app.theme->ui_text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT bd = bi; bd.left = bi.right + app_dpi(6); bd.right = bd.left + app_dpi(64);
    app_fill_round(hdc, &bd, app_dpi(4), g_app.theme->ui_hover);
    app_draw_text(hdc, L"编辑", &bd, g_app.f_ui, g_app.theme->ui_text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    int y = by + app_dpi(34);
    int list_top = y;
    (void)list_top;

    for (int row = 0; row < sb->count; row++) {
        Session *s = &g_app.sessions.items[sb->map[row]];
        int ry = y - sb->scroll;
        if (ry + SIDE_ROW_H < y - sb->scroll) { y += SIDE_ROW_H; continue; }
        if (ry > rc.bottom) break;
        RECT rr = { app_dpi(6), ry, rc.right - app_dpi(10), ry + SIDE_ROW_H - app_dpi(4) };
        if (row == sb->selected) app_fill_round(hdc, &rr, app_dpi(4), g_app.theme->ui_hover);
        else if (row == sb->hot) app_fill_round(hdc, &rr, app_dpi(4), g_app.theme->ui_panel);
        COLORREF dot = g_app.theme->ui_accent;
        if (s->protocol == PROTO_SERIAL) dot = RGB(230, 160, 60);
        else if (s->protocol == PROTO_TELNET) dot = RGB(90, 180, 220);
        else if (s->protocol == PROTO_LOCAL) dot = RGB(120, 200, 120);
        RECT d = { app_dpi(16), ry + app_dpi(14), app_dpi(22), ry + app_dpi(20) };
        app_fill_round(hdc, &d, app_dpi(3), dot);

        RECT nr = { app_dpi(30), ry + app_dpi(6), rc.right - app_dpi(14), ry + app_dpi(26) };
        wchar_t *wn = utf8_to_wide(s->name);
        app_draw_text(hdc, wn, &nr, g_app.f_ui, g_app.theme->ui_text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        free(wn);
        char sub[256];
        session_subtitle(s, sub, 256);
        RECT sr = nr; sr.top = ry + app_dpi(24); sr.bottom = ry + app_dpi(42);
        wchar_t *ws = utf8_to_wide(sub);
        app_draw_text(hdc, ws, &sr, g_app.f_ui, g_app.theme->ui_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        free(ws);
        y += SIDE_ROW_H;
    }

    if (!sb->count) {
        RECT er = { app_dpi(16), y + app_dpi(20), rc.right - app_dpi(16), y + app_dpi(120) };
        app_draw_text(hdc, L"暂无会话\n点击「新建」创建第一个连接", &er, g_app.f_ui, g_app.theme->ui_dim, DT_CENTER | DT_WORDBREAK);
    }

    /* 底部信息 */
    RECT fr = { 0, rc.bottom - app_dpi(26), rc.right, rc.bottom };
    app_fill(hdc, &fr, g_app.theme->ui_bg);
    wchar_t info[64];
    _snwprintf(info, 64, L"%d 个会话  ·  %s", g_app.sessions.n, yt_is_portable() ? L"便携模式" : L"标准模式");
    fr.left += app_dpi(12);
    app_draw_text(hdc, info, &fr, g_app.f_ui, g_app.theme->ui_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    ReleaseDC(sb->hwnd, hdc);
}

static int sb_hit_row(Sidebar *sb, int y) {
    int by = app_dpi(8) + app_dpi(34);
    for (int row = 0; row < sb->count; row++) {
        int ry = by + row * SIDE_ROW_H - sb->scroll;
        if (y >= ry && y < ry + SIDE_ROW_H - app_dpi(4)) return row;
    }
    return -1;
}

static void sb_menu(Sidebar *sb, int row) {
    if (row < 0 || row >= sb->count) return;
    int idx = sb->map[row];
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"连接");
    AppendMenuW(m, MF_STRING, 2, L"在新标签打开");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, 3, L"编辑会话…");
    AppendMenuW(m, MF_STRING, 4, L"复制会话");
    AppendMenuW(m, MF_STRING, 5, L"收藏 / 取消收藏");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, 6, L"删除会话");
    POINT pt; GetCursorPos(&pt);
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN, pt.x, pt.y, 0, sb->hwnd, NULL);
    DestroyMenu(m);
    switch (cmd) {
    case 1: app_connect_session(idx); break;
    case 2: app_connect_session(idx); break;
    case 3: app_show_session_dialog(g_app.main, idx); break;
    case 4: g_app.sessions.items[idx].favorite = g_app.sessions.items[idx].favorite; {
        Session copy = g_app.sessions.items[idx];
        session_generate_id(&copy);
        _snprintf(copy.name, sizeof(copy.name), "%s 副本", g_app.sessions.items[idx].name);
        sessions_add(&g_app.sessions, &copy);
        sessions_save(&g_app.sessions);
        sb_filter(sb);
        InvalidateRect(sb->hwnd, NULL, FALSE);
        sidebar_refresh(sb->hwnd);
    } break;
    case 5: g_app.sessions.items[idx].favorite = !g_app.sessions.items[idx].favorite; sessions_save(&g_app.sessions); sidebar_refresh(sb->hwnd); break;
    case 6: {
        wchar_t msg[256];
        wchar_t *wn = utf8_to_wide(g_app.sessions.items[idx].name);
        _snwprintf(msg, 256, L"确定删除会话「%s」？", wn);
        free(wn);
        if (MessageBoxW(g_app.main, msg, L"YTerminal", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            sessions_remove(&g_app.sessions, idx);
            sessions_save(&g_app.sessions);
            sidebar_refresh(sb->hwnd);
        }
    } break;
    }
}

static LRESULT CALLBACK SidebarEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        PostMessageW(GetParent(h), WM_APP + 1, 0, 0);
        return 0;
    }
    if (msg == WM_CHAR && wp == VK_RETURN) return 0;
    return CallWindowProcW(g_sb_edit_old, h, msg, wp, lp);
}

static LRESULT CALLBACK SidebarProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Sidebar *sb = sb_from(h);
    switch (msg) {
    case WM_CREATE:
        sb = (Sidebar *)xcalloc(1, sizeof(Sidebar));
        sb->hwnd = h;
        sb->hot = -1;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)sb);
        return 0;
    case WM_DESTROY: free(sb->map); free(sb); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); sb_paint(sb); EndPaint(h, &ps); return 0; }
    case WM_SIZE: InvalidateRect(h, NULL, FALSE); return 0;
    case WM_APP + 1: {
        if (sb->count > 0) app_connect_session(sb->map[sb->selected]);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int row = sb_hit_row(sb, GET_Y_LPARAM(lp));
        if (row != sb->hot) { sb->hot = row; InvalidateRect(h, NULL, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE: sb->hot = -1; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        int y = GET_Y_LPARAM(lp), x = GET_X_LPARAM(lp);
        RECT rc; GetClientRect(h, &rc);
        int by = app_dpi(8);
        RECT br = { app_dpi(10), by, app_dpi(10) + app_dpi(64), by + app_dpi(26) };
        RECT bi = br; bi.left = br.right + app_dpi(6); bi.right = bi.left + app_dpi(64);
        RECT bd = bi; bd.left = bi.right + app_dpi(6); bd.right = bd.left + app_dpi(64);
        POINT pt = { x, y };
        if (PtInRect(&br, pt)) { PostMessageW(g_app.main, WM_COMMAND, IDC_SIDE_NEW, 0); return 0; }
        if (PtInRect(&bi, pt)) { PostMessageW(g_app.main, WM_COMMAND, IDC_SIDE_IMPORT, 0); return 0; }
        if (PtInRect(&bd, pt)) { PostMessageW(g_app.main, WM_COMMAND, IDC_EDIT_SESSION, 0); return 0; }
        int row = sb_hit_row(sb, y);
        if (row >= 0) {
            sb->selected = row;
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        int row = sb_hit_row(sb, GET_Y_LPARAM(lp));
        if (row >= 0) { sb->selected = row; app_connect_session(sb->map[row]); }
        return 0;
    }
    case WM_RBUTTONUP: {
        int row = sb_hit_row(sb, GET_Y_LPARAM(lp));
        if (row >= 0) { sb->selected = row; InvalidateRect(h, NULL, FALSE); sb_menu(sb, row); }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        sb->scroll -= delta * 48;
        if (sb->scroll < 0) sb->scroll = 0;
        RECT cr; GetClientRect(h, &cr);
        int maxs = sb->count * SIDE_ROW_H - (cr.bottom - app_dpi(110));
        if (maxs < 0) maxs = 0;
        if (sb->scroll > maxs) sb->scroll = maxs;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

HWND sidebar_create(HWND parent, HINSTANCE inst) {
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = SidebarProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"YSidebar";
    RegisterClassExW(&wc);
    HWND h = CreateWindowExW(0, L"YSidebar", L"", WS_CHILD | WS_VISIBLE, 0, 0, 240, 400, parent, NULL, inst, NULL);
    Sidebar *sb = sb_from(h);

    wchar_t *cue = L"搜索会话…";
    HWND e = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                             0, 0, 10, 10, h, (HMENU)IDC_SEARCH, inst, NULL);
    sb->search = e;
    SendMessageW(e, WM_SETFONT, (WPARAM)g_app.f_ui, TRUE);
    SendMessageW(e, EM_SETCUEBANNER, TRUE, (LPARAM)cue);
    g_sb_edit_old = (WNDPROC)SetWindowLongPtrW(e, GWLP_WNDPROC, (LONG_PTR)SidebarEditProc);
    return h;
}

void sidebar_resize(HWND h, RECT *rc) {
    MoveWindow(h, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, TRUE);
    Sidebar *sb = sb_from(h);
    if (!sb) return;
    int w = rc->right - rc->left;
    MoveWindow(sb->search, app_dpi(10), app_dpi(44), w - app_dpi(20), app_dpi(24), TRUE);
}

void sidebar_refresh(HWND h) {
    Sidebar *sb = sb_from(h);
    if (!sb) return;
    sb_filter(sb);
    InvalidateRect(h, NULL, FALSE);
}

/* ================================================================== */
/* 状态栏                                                              */
/* ================================================================== */
typedef struct { HWND hwnd; wchar_t msg[256]; } StatusBar;

static StatusBar *st_from(HWND h) { return (StatusBar *)GetWindowLongPtrW(h, GWLP_USERDATA); }

static void st_paint(StatusBar *st) {
    RECT rc;
    GetClientRect(st->hwnd, &rc);
    HDC hdc = GetDC(st->hwnd);
    app_fill(hdc, &rc, g_app.theme->ui_panel);
    RECT line = { 0, 0, rc.right, 1 };
    app_fill(hdc, &line, g_app.theme->ui_border);
    RECT lr = rc;
    lr.left += app_dpi(10);
    lr.right -= app_dpi(12);
    app_draw_text(hdc, st->msg, &lr, g_app.f_ui, g_app.theme->ui_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    HWND v = app_active_view();
    if (v) {
        Session *s = term_view_session(v);
        wchar_t info[256];
        const wchar_t *state = term_view_status_text(v);
        int cols = term_view_cols(v), rows = term_view_rows(v);
        if (s) {
            wchar_t *wp = utf8_to_wide(protocol_name(s->protocol));
            _snwprintf(info, 256, L"%s  |  %d×%d  |  %s%s%s", wp ? wp : L"", cols, rows,
                       state, g_app.broadcast ? L"  |  广播中" : L"",
                       term_view_is_logging(v) ? L"  |  日志" : L"");
            free(wp);
        } else {
            _snwprintf(info, 256, L"%d×%d  |  %s", cols, rows, state);
        }
        int tw = app_text_width(hdc, g_app.f_ui, info);
        RECT ir = { rc.right - tw - app_dpi(14), 0, rc.right - app_dpi(12), rc.bottom };
        app_draw_text(hdc, info, &ir, g_app.f_ui, g_app.theme->ui_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    ReleaseDC(st->hwnd, hdc);
}

static LRESULT CALLBACK StatusProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    StatusBar *st = st_from(h);
    switch (msg) {
    case WM_CREATE:
        st = (StatusBar *)xcalloc(1, sizeof(StatusBar));
        st->hwnd = h;
        wcsncpy(st->msg, L"就绪", 255);
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)st);
        return 0;
    case WM_DESTROY: free(st); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); st_paint(st); EndPaint(h, &ps); return 0; }
    case WM_SIZE: InvalidateRect(h, NULL, FALSE); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

HWND statusbar_create(HWND parent, HINSTANCE inst) {
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = StatusProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"YStatus";
    RegisterClassExW(&wc);
    return CreateWindowExW(0, L"YStatus", L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, YT_STATUS_H, parent, NULL, inst, NULL);
}

void statusbar_resize(HWND h, RECT *rc) { MoveWindow(h, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, TRUE); }
void statusbar_update(HWND h) { InvalidateRect(h, NULL, FALSE); }

void app_status_set(const wchar_t *text) {
    StatusBar *st = st_from(g_app.statusbar);
    if (!st) return;
    wcsncpy(st->msg, text ? text : L"", 255);
    st->msg[255] = 0;
    InvalidateRect(g_app.statusbar, NULL, FALSE);
}

void app_status_refresh(void) {
    if (!g_app.statusbar) return;
    InvalidateRect(g_app.statusbar, NULL, FALSE);
}
