#include "yt_core.h"

/* COM enum, macro store, command history for autocomplete */

int yt_com_enum(wchar_t names[][32], int cap)
{
    int n = 0, i;
    for (i = 1; i <= 32 && n < cap; i++) {
        wchar_t path[64];
        HANDLE h;
        swprintf(path, 64, L"\\\\.\\COM%d", i);
        h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            swprintf(names[n], 32, L"COM%d", i);
            n++;
            CloseHandle(h);
        }
    }
    return n;
}

#define MACRO_MAX 64
#define MACRO_LEN 512
static wchar_t g_macros[MACRO_MAX][MACRO_LEN];
static int g_macro_n;
static BOOL g_macro_rec;
static wchar_t g_hist[128][256];
static int g_hist_n;

void yt_macro_rec_start(void) { g_macro_rec = TRUE; g_macro_n = 0; }
void yt_macro_rec_stop(void) { g_macro_rec = FALSE; }
BOOL yt_macro_recording(void) { return g_macro_rec; }

void yt_macro_push(const wchar_t *line)
{
    if (!g_macro_rec || !line || g_macro_n >= MACRO_MAX) return;
    wcsncpy(g_macros[g_macro_n++], line, MACRO_LEN - 1);
}

int yt_macro_count(void) { return g_macro_n; }
const wchar_t *yt_macro_get(int i) { return (i >= 0 && i < g_macro_n) ? g_macros[i] : L""; }

BOOL yt_macro_save(void)
{
    FILE *fp; char path[YT_PATH_MAX]; int i;
    WideCharToMultiByte(CP_UTF8, 0, yt_paths()->config, -1, path, sizeof(path), NULL, NULL);
    strncat(path, "\\macros.txt", sizeof(path) - strlen(path) - 1);
    fp = fopen(path, "wb");
    if (!fp) return FALSE;
    for (i = 0; i < g_macro_n; i++) {
        char u[MACRO_LEN * 3];
        WideCharToMultiByte(CP_UTF8, 0, g_macros[i], -1, u, sizeof(u), NULL, NULL);
        fprintf(fp, "%s\n", u);
    }
    fclose(fp);
    return TRUE;
}

int yt_macro_load(void)
{
    FILE *fp; char path[YT_PATH_MAX], line[MACRO_LEN * 3];
    WideCharToMultiByte(CP_UTF8, 0, yt_paths()->config, -1, path, sizeof(path), NULL, NULL);
    strncat(path, "\\macros.txt", sizeof(path) - strlen(path) - 1);
    fp = fopen(path, "rb");
    if (!fp) return 0;
    g_macro_n = 0;
    while (fgets(line, sizeof(line), fp) && g_macro_n < MACRO_MAX) {
        size_t L = strlen(line);
        while (L && (line[L-1] == '\n' || line[L-1] == '\r')) line[--L] = 0;
        MultiByteToWideChar(CP_UTF8, 0, line, -1, g_macros[g_macro_n++], MACRO_LEN);
    }
    fclose(fp);
    return g_macro_n;
}

void yt_hist_add(const wchar_t *cmd)
{
    if (!cmd || !cmd[0]) return;
    if (g_hist_n < 128) wcsncpy(g_hist[g_hist_n++], cmd, 255);
    else {
        memmove(g_hist, g_hist + 1, sizeof(g_hist) - sizeof(g_hist[0]));
        wcsncpy(g_hist[127], cmd, 255);
    }
}

int yt_hist_complete(const wchar_t *prefix, wchar_t *out, int cap)
{
    int i, plen;
    if (!prefix || !prefix[0]) return 0;
    plen = (int)wcslen(prefix);
    for (i = g_hist_n - 1; i >= 0; i--) {
        if (wcsncmp(g_hist[i], prefix, plen) == 0) {
            wcsncpy(out, g_hist[i], cap - 1);
            out[cap - 1] = 0;
            return 1;
        }
    }
    return 0;
}

static void sleep_pump(DWORD ms)
{
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < ms) {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    }
}

BOOL yt_script_run_file(YtSession *s, const wchar_t *path)
{
    FILE *fp; char line[1024], upath[YT_PATH_MAX];
    if (!s || s->state != YT_ST_CONNECTED) return FALSE;
    WideCharToMultiByte(CP_UTF8, 0, path, -1, upath, sizeof(upath), NULL, NULL);
    fp = fopen(upath, "rb");
    if (!fp) return FALSE;
    while (fgets(line, sizeof(line), fp)) {
        size_t L = strlen(line);
        while (L && (line[L-1] == '\n' || line[L-1] == '\r')) line[--L] = 0;
        if (!L || line[0] == '#') continue;
        yt_session_write(s, line, (int)L);
        yt_session_write(s, "\r", 1);
        sleep_pump(80); /* S-04：泵消息，避免 UI 假死 */
    }
    fclose(fp);
    return TRUE;
}

void yt_sys_stats(wchar_t *out, int cap)
{
    MEMORYSTATUSEX ms; FILETIME idle, ker, user;
    static ULARGE_INTEGER pidle, pker, puser;
    ULARGE_INTEGER i, k, u;
    double cpu = 0;
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    if (GetSystemTimes(&idle, &ker, &user)) {
        memcpy(&i, &idle, sizeof(i));
        memcpy(&k, &ker, sizeof(k));
        memcpy(&u, &user, sizeof(u));
        if (pker.QuadPart) {
            ULONGLONG sidle = i.QuadPart - pidle.QuadPart;
            ULONGLONG stot = (k.QuadPart - pker.QuadPart) + (u.QuadPart - puser.QuadPart);
            if (stot) cpu = 100.0 - (100.0 * sidle / stot);
        }
        pidle = i; pker = k; puser = u;
    }
    swprintf(out, cap, L"CPU %.0f%% | RAM %llu/%llu MB",
             cpu, (unsigned long long)(ms.ullTotalPhys - ms.ullAvailPhys) / (1024*1024),
             (unsigned long long)ms.ullTotalPhys / (1024*1024));
}

static int g_paste_delay_ms;
void yt_set_paste_delay(int ms) { g_paste_delay_ms = ms < 0 ? 0 : ms; }
int yt_get_paste_delay(void) { return g_paste_delay_ms; }
