#include "app.h"

static const wchar_t *kind_tag(YtKind k)
{
    if (k == YT_KIND_SSH) return L"[SSH] ";
    if (k == YT_KIND_SERIAL) return L"[串口] ";
    if (k == YT_KIND_LOCAL) return L"[本地] ";
    if (k == YT_KIND_TELNET) return L"[Telnet] ";
    return L"";
}

static HTREEITEM find_or_add_group(HTREEITEM root, const wchar_t *group)
{
    HTREEITEM child;
    TVITEMW it = {0};
    wchar_t text[96];
    TVINSERTSTRUCTW is = {0};
    if (!group || !group[0]) group = L"未分组";
    child = TreeView_GetChild(g.tree, root);
    while (child) {
        it.mask = TVIF_TEXT | TVIF_PARAM;
        it.hItem = child;
        it.pszText = text;
        it.cchTextMax = 96;
        TreeView_GetItem(g.tree, &it);
        if (wcscmp(text, group) == 0) return child;
        child = TreeView_GetNextSibling(g.tree, child);
    }
    is.hParent = root;
    is.hInsertAfter = TVI_LAST;
    is.item.mask = TVIF_TEXT | TVIF_PARAM;
    is.item.pszText = (LPWSTR)group;
    is.item.lParam = -1;
    return TreeView_InsertItem(g.tree, &is);
}

void yt_app_profile_from_session(YtSession *dst, const YtSession *src)
{
    yt_session_init(dst);
    dst->kind = src->kind;
    dst->port = src->port;
    dst->baud = src->baud;
    dst->jump_port = src->jump_port;
    dst->data_bits = src->data_bits;
    dst->stop_bits = src->stop_bits;
    dst->parity = src->parity;
    dst->flow = src->flow;
    wcsncpy(dst->name, src->name[0] ? src->name : src->title, YT_TITLE_MAX - 1);
    wcsncpy(dst->group, src->group, 63);
    wcsncpy(dst->host, src->host, YT_HOST_MAX - 1);
    wcsncpy(dst->user, src->user, 63);
    wcsncpy(dst->pass, src->pass, 127);
    wcsncpy(dst->com, src->com, 31);
    wcsncpy(dst->shell, src->shell, 63);
    wcsncpy(dst->keyfile, src->keyfile, YT_PATH_MAX - 1);
    wcsncpy(dst->jump_host, src->jump_host, YT_HOST_MAX - 1);
    wcsncpy(dst->jump_user, src->jump_user, 63);
    wcsncpy(dst->jump_pass, src->jump_pass, 127);
    strncpy(dst->forwards, src->forwards, sizeof(dst->forwards) - 1);
    if (!dst->group[0]) {
        if (dst->kind == YT_KIND_SSH) wcscpy(dst->group, L"SSH");
        else if (dst->kind == YT_KIND_SERIAL) wcscpy(dst->group, L"串口");
        else if (dst->kind == YT_KIND_TELNET) wcscpy(dst->group, L"Telnet");
        else if (dst->kind == YT_KIND_LOCAL) wcscpy(dst->group, L"本地");
        else wcscpy(dst->group, L"其他");
    }
}

void yt_app_tree_reload(void)
{
    int i;
    HTREEITEM root, gitem;
    TVINSERTSTRUCTW is = {0};
    wchar_t label[YT_TITLE_MAX + 32];
    if (!g.tree) return;
    TreeView_DeleteAllItems(g.tree);
    is.hParent = TVI_ROOT;
    is.hInsertAfter = TVI_LAST;
    is.item.mask = TVIF_TEXT | TVIF_PARAM;
    is.item.pszText = L"会话书签";
    is.item.lParam = -1;
    root = TreeView_InsertItem(g.tree, &is);
    for (i = 0; i < g.tree_n; i++) {
        HTREEITEM grp = find_or_add_group(root, g.profiles[i].group);
        const wchar_t *nm = g.profiles[i].name[0] ? g.profiles[i].name :
                            (g.profiles[i].host[0] ? g.profiles[i].host : g.profiles[i].com);
        swprintf(label, YT_TITLE_MAX + 32, L"%ls%ls", kind_tag(g.profiles[i].kind), nm ? nm : L"(未命名)");
        is.hParent = grp;
        is.hInsertAfter = TVI_LAST;
        is.item.mask = TVIF_TEXT | TVIF_PARAM;
        is.item.pszText = label;
        is.item.lParam = i;
        TreeView_InsertItem(g.tree, &is);
    }
    TreeView_Expand(g.tree, root, TVE_EXPAND);
    gitem = TreeView_GetChild(g.tree, root);
    while (gitem) {
        TreeView_Expand(g.tree, gitem, TVE_EXPAND);
        gitem = TreeView_GetNextSibling(g.tree, gitem);
    }
}

void yt_app_sessions_persist(void)
{
    yt_profile_save(g.profiles, g.tree_n);
}

void yt_app_sessions_reload(void)
{
    int i;
    for (i = 0; i < g.tree_n; i++)
        DeleteCriticalSection(&g.profiles[i].lock);
    g.tree_n = yt_profile_load(g.profiles, YT_MAX_SESSIONS);
    if (g.tree_n <= 0) {
        /* 文件存在但解析失败时不静默覆盖（S-05） */
        if (GetFileAttributesW(yt_paths()->sessions) != INVALID_FILE_ATTRIBUTES) {
            g.tree_n = 0;
            yt_app_set_status(L"sessions.json 无效，已保留原文件");
        } else {
            g.tree_n = yt_profile_ensure_defaults(g.profiles, YT_MAX_SESSIONS);
        }
    }
    yt_app_tree_reload();
}

int yt_app_tree_sel_index(void)
{
    HTREEITEM sel = TreeView_GetSelection(g.tree);
    TVITEMW it = {0};
    it.mask = TVIF_PARAM;
    it.hItem = sel;
    if (!sel || !TreeView_GetItem(g.tree, &it)) return -1;
    if (it.lParam < 0 || it.lParam >= g.tree_n) return -1;
    return (int)it.lParam;
}

void yt_app_tree_connect_index(int i)
{
    YtSession *s;
    if (i < 0 || i >= g.tree_n) return;
    s = (YtSession *)calloc(1, sizeof(YtSession));
    yt_app_profile_from_session(s, &g.profiles[i]);
    wcsncpy(s->pass, g.profiles[i].pass, 127);
    if (s->kind == YT_KIND_SSH) {
        swprintf(s->title, YT_TITLE_MAX, L"%ls@%ls:%d", s->user, s->host, s->port);
        yt_app_add_tab(s);
        if (!yt_ssh_start_async(g.hwnd, s)) yt_app_close_tab(g.ntabs - 1);
    } else if (s->kind == YT_KIND_SERIAL) {
        yt_app_add_tab(s);
        if (!yt_serial_start(s)) yt_app_start_ok(s, FALSE);
        else yt_app_set_status(L"已连接");
    } else if (s->kind == YT_KIND_TELNET) {
        yt_app_add_tab(s);
        if (!yt_telnet_start(s)) yt_app_start_ok(s, FALSE);
        else yt_app_set_status(L"已连接");
    } else {
        swprintf(s->title, YT_TITLE_MAX, L"Local:%ls", s->shell[0] ? s->shell : L"powershell");
        yt_app_add_tab(s);
        if (!yt_local_start(s)) yt_app_start_ok(s, FALSE);
        else yt_app_set_status(L"已连接");
    }
}

/* 只搬书签字段，不动 CRITICAL_SECTION（S-01/G-02） */
static void profile_fields_only(YtSession *d, const YtSession *s)
{
    d->kind = s->kind;
    d->port = s->port;
    d->baud = s->baud;
    d->jump_port = s->jump_port;
    d->data_bits = s->data_bits;
    d->stop_bits = s->stop_bits;
    d->parity = s->parity;
    d->flow = s->flow;
    wcsncpy(d->name, s->name, YT_TITLE_MAX - 1);
    wcsncpy(d->group, s->group, 63);
    wcsncpy(d->host, s->host, YT_HOST_MAX - 1);
    wcsncpy(d->user, s->user, 63);
    wcsncpy(d->pass, s->pass, 127);
    wcsncpy(d->com, s->com, 31);
    wcsncpy(d->shell, s->shell, 63);
    wcsncpy(d->keyfile, s->keyfile, YT_PATH_MAX - 1);
    wcsncpy(d->jump_host, s->jump_host, YT_HOST_MAX - 1);
    wcsncpy(d->jump_user, s->jump_user, 63);
    wcsncpy(d->jump_pass, s->jump_pass, 127);
    strncpy(d->forwards, s->forwards, sizeof(d->forwards) - 1);
    d->forwards[sizeof(d->forwards) - 1] = 0;
}

void yt_app_tree_delete_index(int i)
{
    int j;
    if (i < 0 || i >= g.tree_n) return;
    for (j = i; j < g.tree_n - 1; j++)
        profile_fields_only(&g.profiles[j], &g.profiles[j + 1]);
    DeleteCriticalSection(&g.profiles[g.tree_n - 1].lock);
    memset(&g.profiles[g.tree_n - 1], 0, sizeof(YtSession));
    g.tree_n--;
    yt_app_sessions_persist();
    yt_app_tree_reload();
}
