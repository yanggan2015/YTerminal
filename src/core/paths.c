#include "yt_core.h"
#include <shlobj.h>

static YtPaths g_paths;

static void ensure_dir(const wchar_t *p)
{
    CreateDirectoryW(p, NULL);
}

void yt_paths_init(YtPaths *p)
{
    wchar_t *slash;
    GetModuleFileNameW(NULL, p->root, YT_PATH_MAX);
    slash = wcsrchr(p->root, L'\\');
    if (slash) *slash = 0;
    swprintf(p->config, YT_PATH_MAX, L"%ls\\config", p->root);
    swprintf(p->sessions, YT_PATH_MAX, L"%ls\\config\\sessions.json", p->root);
    swprintf(p->window, YT_PATH_MAX, L"%ls\\config\\window.json", p->root);
    swprintf(p->creds, YT_PATH_MAX, L"%ls\\config\\creds.bin", p->root);
    swprintf(p->logs, YT_PATH_MAX, L"%ls\\logs", p->root);
    swprintf(p->themes, YT_PATH_MAX, L"%ls\\themes", p->root);
    swprintf(p->runtime, YT_PATH_MAX, L"%ls\\runtime", p->root);
    swprintf(p->keys, YT_PATH_MAX, L"%ls\\keys", p->root);
    ensure_dir(p->config);
    ensure_dir(p->logs);
    ensure_dir(p->themes);
    ensure_dir(p->runtime);
    ensure_dir(p->keys);
    g_paths = *p;
}

const YtPaths *yt_paths(void) { return &g_paths; }
