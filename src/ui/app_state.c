#include "app.h"

App g;

void yt_app_set_status(const wchar_t *s)
{
    if (g.status) SetWindowTextW(g.status, s);
}

int yt_app_cur_tab(void)
{
    if (!g.tab) return -1;
    return (int)SendMessageW(g.tab, TCM_GETCURSEL, 0, 0);
}

int yt_app_tab_count(void)
{
    return g.ntabs;
}

YtSession *yt_app_tab_at(int i)
{
    if (i < 0 || i >= g.ntabs) return NULL;
    return g.sess[i];
}

int yt_app_kind_sel(void)
{
    if (!g.cmb) return 0;
    return (int)SendMessageW(g.cmb, CB_GETCURSEL, 0, 0);
}
