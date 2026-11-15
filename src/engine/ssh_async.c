#include "yt_core.h"

#define WM_YT_SSH_DONE (WM_APP + 210)

typedef struct {
    YtSession *s;
    HWND hwnd;
} SshJob;

extern BOOL yt_ssh_start(YtSession *s);

static DWORD WINAPI ssh_job(LPVOID arg)
{
    SshJob *j = (SshJob *)arg;
    BOOL ok = yt_ssh_start(j->s);
    PostMessageW(j->hwnd, WM_YT_SSH_DONE, (WPARAM)ok, (LPARAM)j->s);
    free(j);
    return 0;
}

BOOL yt_ssh_start_async(HWND hwnd, YtSession *s)
{
    SshJob *j = (SshJob *)malloc(sizeof(SshJob));
    if (!j) return FALSE;
    j->s = s;
    j->hwnd = hwnd;
    s->state = YT_ST_CONNECTING;
    if (!CreateThread(NULL, 0, ssh_job, j, 0, NULL)) {
        free(j);
        return FALSE;
    }
    return TRUE;
}
