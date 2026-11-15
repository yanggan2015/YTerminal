#include "app.h"
#include "ui_term.h"
#include <windowsx.h>
#include <stdlib.h>

typedef struct {
    HWND a, b;
    int vertical;   /* 1 = 左右分，0 = 上下分 */
    float ratio;
    int drag;
} Split;

static Split *sp_from(HWND h) { return (Split *)GetWindowLongPtrW(h, GWLP_USERDATA); }

#define DIV 2

static void sp_layout(Split *sp, int w, int h) {
    if (sp->vertical) {
        int aw = (int)(w * sp->ratio);
        if (aw < 80) aw = 80;
        if (aw > w - 80) aw = w - 80;
        MoveWindow(sp->a, 0, 0, aw - DIV / 2, h, TRUE);
        MoveWindow(sp->b, aw + DIV / 2, 0, w - aw - DIV / 2, h, TRUE);
    } else {
        int ah = (int)(h * sp->ratio);
        if (ah < 60) ah = 60;
        if (ah > h - 60) ah = h - 60;
        MoveWindow(sp->a, 0, 0, w, ah - DIV / 2, TRUE);
        MoveWindow(sp->b, 0, ah + DIV / 2, w, h - ah - DIV / 2, TRUE);
    }
}

static LRESULT CALLBACK SplitProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Split *sp = sp_from(h);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        sp = (Split *)cs->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)sp);
        return 0;
    }
    case WM_SIZE: if (sp) sp_layout(sp, LOWORD(lp), HIWORD(lp)); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        app_fill(hdc, &rc, g_app.theme->bg);
        RECT div;
        if (sp && sp->vertical) {
            int x = (int)(rc.right * sp->ratio) - DIV / 2;
            div = (RECT){ x, 0, x + DIV, rc.bottom };
        } else {
            int y = (int)(rc.bottom * sp->ratio) - DIV / 2;
            div = (RECT){ 0, y, rc.right, y + DIV };
        }
        app_fill(hdc, &div, sp && sp->drag ? g_app.theme->ui_accent : g_app.theme->ui_border);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SETCURSOR:
        if (sp && LOWORD(lp) == HTCLIENT) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(h, &pt);
            RECT rc; GetClientRect(h, &rc);
            int on_near = sp->vertical ? (abs(pt.x - (int)(rc.right * sp->ratio)) <= 4)
                                   : (abs(pt.y - (int)(rc.bottom * sp->ratio)) <= 4);
            SetCursor(LoadCursorW(NULL, (LPCWSTR)(on_near ? (sp->vertical ? IDC_SIZEWE : IDC_SIZENS) : IDC_ARROW)));
            if (on_near) return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        RECT rc; GetClientRect(h, &rc);
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int on_near = sp->vertical ? (abs(x - (int)(rc.right * sp->ratio)) <= 5)
                               : (abs(y - (int)(rc.bottom * sp->ratio)) <= 5);
        if (on_near) { sp->drag = 1; SetCapture(h); InvalidateRect(h, NULL, FALSE); }
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (sp->drag) {
            RECT rc; GetClientRect(h, &rc);
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            float r = sp->vertical ? (float)x / (rc.right ? rc.right : 1) : (float)y / (rc.bottom ? rc.bottom : 1);
            if (r < 0.12f) r = 0.12f;
            if (r > 0.88f) r = 0.88f;
            sp->ratio = r;
            sp_layout(sp, rc.right, rc.bottom);
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        RECT rc; GetClientRect(h, &rc);
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int on_near = sp->vertical ? (abs(x - (int)(rc.right * sp->ratio)) <= 5)
                               : (abs(y - (int)(rc.bottom * sp->ratio)) <= 5);
        SetCursor(LoadCursorW(NULL, (LPCWSTR)(on_near ? (sp->vertical ? IDC_SIZEWE : IDC_SIZENS) : IDC_ARROW)));
        return 0;
    }
    case WM_LBUTTONUP:
        if (sp->drag) { sp->drag = 0; ReleaseCapture(); InvalidateRect(h, NULL, FALSE); }
        return 0;
    case WM_DESTROY: free(sp); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

HWND split_create(HWND parent, HWND a, HWND b, int vertical) {
    static int cls_ok = 0;
    if (!cls_ok) {
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SplitProc;
        wc.hInstance = g_app.inst;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = L"YSplit";
        RegisterClassExW(&wc);
        cls_ok = 1;
    }
    Split *sp = (Split *)xcalloc(1, sizeof(Split));
    sp->a = a; sp->b = b; sp->vertical = vertical; sp->ratio = 0.5f;
    HWND h = CreateWindowExW(0, L"YSplit", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                             0, 0, 400, 300, parent, NULL, g_app.inst, sp);
    if (!h) { free(sp); return NULL; }
    SetParent(a, h);
    SetParent(b, h);
    ShowWindow(a, SW_SHOW);
    ShowWindow(b, SW_SHOW);
    RECT rc; GetClientRect(h, &rc);
    sp_layout(sp, rc.right, rc.bottom);
    return h;
}

void split_destroy(HWND h) {
    Split *sp = sp_from(h);
    if (!sp) return;
    HWND a = sp->a, b = sp->b;
    SetWindowLongPtrW(h, GWLP_USERDATA, 0);
    DestroyWindow(h);
    if (a) app_destroy_root(a);
    if (b) app_destroy_root(b);
}

void split_set_ratio(HWND h, float ratio) {
    Split *sp = sp_from(h);
    if (!sp) return;
    sp->ratio = ratio;
    RECT rc; GetClientRect(h, &rc);
    sp_layout(sp, rc.right, rc.bottom);
}

int split_is_split(HWND h) {
    wchar_t cls[64] = { 0 };
    if (!h || !GetClassNameW(h, cls, 64)) return 0;
    return wcscmp(cls, L"YSplit") == 0;
}
