#include "session.h"
#include "json.h"
#include <wincrypt.h>
#include <shlobj.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* 路径                                                                 */
/* ------------------------------------------------------------------ */
void yt_data_dir(wchar_t *out, int cch) {
    wchar_t exe[MAX_PATH];
    wchar_t marker[MAX_PATH];
    yt_exe_dir(exe, MAX_PATH);
    _snwprintf(marker, MAX_PATH, L"%s\\portable.txt", exe);
    marker[MAX_PATH - 1] = 0;
    if (GetFileAttributesW(marker) != INVALID_FILE_ATTRIBUTES) {
        _snwprintf(out, (size_t)cch, L"%s\\data", exe);
        yt_mkdir(out);
        return;
    }
    yt_app_dir(out, cch);
    yt_mkdir(out);
}
int yt_is_portable(void) {
    wchar_t exe[MAX_PATH], marker[MAX_PATH];
    yt_exe_dir(exe, MAX_PATH);
    _snwprintf(marker, MAX_PATH, L"%s\\portable.txt", exe);
    marker[MAX_PATH - 1] = 0;
    return GetFileAttributesW(marker) != INVALID_FILE_ATTRIBUTES;
}

/* ------------------------------------------------------------------ */
/* DPAPI                                                                */
/* ------------------------------------------------------------------ */
char *yt_dpapi_encrypt_hex(const char *plain) {
    if (!plain || !*plain) return NULL;
    DATA_BLOB in, out;
    in.pbData = (BYTE *)plain;
    in.cbData = (DWORD)strlen(plain);
    out.pbData = NULL; out.cbData = 0;
    if (!CryptProtectData(&in, L"YTerminal", NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out)) return NULL;
    char *hex = (char *)xmalloc((size_t)out.cbData * 2 + 1);
    for (DWORD i = 0; i < out.cbData; i++) _snprintf(hex + i * 2, 3, "%02x", out.pbData[i]);
    hex[out.cbData * 2] = 0;
    LocalFree(out.pbData);
    return hex;
}

char *yt_dpapi_decrypt_hex(const char *hex) {
    if (!hex || !*hex) return NULL;
    size_t n = strlen(hex) / 2;
    BYTE *buf = (BYTE *)xmalloc(n + 1);
    for (size_t i = 0; i < n; i++) {
        unsigned v = 0;
        sscanf(hex + i * 2, "%2x", &v);
        buf[i] = (BYTE)v;
    }
    DATA_BLOB in, out;
    in.pbData = buf; in.cbData = (DWORD)n;
    out.pbData = NULL; out.cbData = 0;
    char *plain = NULL;
    if (CryptUnprotectData(&in, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        plain = (char *)xmalloc((size_t)out.cbData + 1);
        memcpy(plain, out.pbData, out.cbData);
        plain[out.cbData] = 0;
        LocalFree(out.pbData);
    }
    free(buf);
    return plain;
}

/* ------------------------------------------------------------------ */
/* 设置                                                                 */
/* ------------------------------------------------------------------ */
void settings_defaults(Settings *s) {
    memset(s, 0, sizeof(*s));
    s->theme = 0;
    strcpy(s->font, "Consolas");
    s->font_size = 12;
    s->line_pad = 1;
    s->cursor_style = 0;
    s->cursor_blink = 1;
    s->scrollback = 5000;
    s->copy_on_select = 1;
    s->right_click_paste = 0;
    s->confirm_paste = 1;
    s->restore_tabs = 1;
    s->save_on_exit = 1;
    s->bell = 1;
    s->opacity = 100;
    s->show_sidebar = 1;
    s->show_statusbar = 1;
    s->show_toolbar = 1;
    s->sidebar_width = 232;
    s->tab_width = 180;
    s->wheel_zoom = 1;
    s->alt_send_esc = 1;
    s->broadcast_default = 0;
    s->log_auto = 0;
    s->download_dir[0] = 0;
    s->ui_lang = 0;
    s->ssh_keepalive = 30;
    s->double_click_copy = 1;
    s->middle_click_paste = 1;
    s->show_tab_close = 1;
    s->confirm_close = 1;
    s->smooth_scroll = 1;
    s->status_clock = 1;
    s->win_x = s->win_y = s->win_w = s->win_h = -1;
    strcpy(s->highlights,
           "ERROR=>#FF6B68;WARN=>#E5C07B;FAILED=>#FF6B68;"
           "SUCCESS=>#98C379;拒绝访问=>#FF6B68;Permission denied=>#FF6B68");
    wchar_t data[MAX_PATH], home[MAX_PATH] = {0};
    yt_data_dir(data, MAX_PATH);
    _snwprintf(s->log_dir, MAX_PATH, L"%s\\logs", data);
    s->log_dir[MAX_PATH - 1] = 0;
    SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, home);
    _snwprintf(s->download_dir, MAX_PATH, L"%s\\Downloads", home);
    s->download_dir[MAX_PATH - 1] = 0;
    yt_mkdir(s->log_dir);
}

static void settings_to_json(JVal *o, Settings *s) {
    json_set_int(o, "theme", s->theme);
    json_set_str(o, "font", s->font);
    json_set_int(o, "font_size", s->font_size);
    json_set_int(o, "line_pad", s->line_pad);
    json_set_int(o, "cursor_style", s->cursor_style);
    json_set_int(o, "cursor_blink", s->cursor_blink);
    json_set_int(o, "scrollback", s->scrollback);
    json_set_int(o, "copy_on_select", s->copy_on_select);
    json_set_int(o, "right_click_paste", s->right_click_paste);
    json_set_int(o, "confirm_paste", s->confirm_paste);
    json_set_int(o, "restore_tabs", s->restore_tabs);
    json_set_int(o, "save_on_exit", s->save_on_exit);
    json_set_int(o, "bell", s->bell);
    json_set_int(o, "opacity", s->opacity);
    json_set_int(o, "show_sidebar", s->show_sidebar);
    json_set_int(o, "show_statusbar", s->show_statusbar);
    json_set_int(o, "show_toolbar", s->show_toolbar);
    json_set_int(o, "sidebar_width", s->sidebar_width);
    json_set_int(o, "tab_width", s->tab_width);
    json_set_int(o, "wheel_zoom", s->wheel_zoom);
    json_set_int(o, "alt_send_esc", s->alt_send_esc);
    json_set_int(o, "broadcast_default", s->broadcast_default);
    json_set_int(o, "log_auto", s->log_auto);
    {
        char *u = wide_to_utf8(s->log_dir);
        json_set_str(o, "log_dir", u);
        free(u);
        u = wide_to_utf8(s->download_dir);
        json_set_str(o, "download_dir", u);
        free(u);
    }
    json_set_int(o, "ui_lang", s->ui_lang);
    json_set_int(o, "ssh_keepalive", s->ssh_keepalive);
    json_set_int(o, "double_click_copy", s->double_click_copy);
    json_set_int(o, "middle_click_paste", s->middle_click_paste);
    json_set_int(o, "show_tab_close", s->show_tab_close);
    json_set_int(o, "confirm_close", s->confirm_close);
    json_set_int(o, "smooth_scroll", s->smooth_scroll);
    json_set_int(o, "status_clock", s->status_clock);
    json_set_str(o, "highlights", s->highlights);
    json_set_int(o, "win_x", s->win_x);
    json_set_int(o, "win_y", s->win_y);
    json_set_int(o, "win_w", s->win_w);
    json_set_int(o, "win_h", s->win_h);
}

static void json_to_settings(JVal *o, Settings *s) {
    s->theme = json_int(o, "theme", s->theme);
    const char *f = json_str(o, "font", s->font);
    if (f) { strncpy(s->font, f, sizeof(s->font) - 1); s->font[sizeof(s->font) - 1] = 0; }
    s->font_size = json_int(o, "font_size", s->font_size);
    s->line_pad = json_int(o, "line_pad", s->line_pad);
    s->cursor_style = json_int(o, "cursor_style", s->cursor_style);
    s->cursor_blink = json_int(o, "cursor_blink", s->cursor_blink);
    s->scrollback = json_int(o, "scrollback", s->scrollback);
    s->copy_on_select = json_int(o, "copy_on_select", s->copy_on_select);
    s->right_click_paste = json_int(o, "right_click_paste", s->right_click_paste);
    s->confirm_paste = json_int(o, "confirm_paste", s->confirm_paste);
    s->restore_tabs = json_int(o, "restore_tabs", s->restore_tabs);
    s->save_on_exit = json_int(o, "save_on_exit", s->save_on_exit);
    s->bell = json_int(o, "bell", s->bell);
    s->opacity = json_int(o, "opacity", s->opacity);
    s->show_sidebar = json_int(o, "show_sidebar", s->show_sidebar);
    s->show_statusbar = json_int(o, "show_statusbar", s->show_statusbar);
    s->show_toolbar = json_int(o, "show_toolbar", s->show_toolbar);
    s->sidebar_width = json_int(o, "sidebar_width", s->sidebar_width);
    s->tab_width = json_int(o, "tab_width", s->tab_width);
    s->wheel_zoom = json_int(o, "wheel_zoom", s->wheel_zoom);
    s->alt_send_esc = json_int(o, "alt_send_esc", s->alt_send_esc);
    s->broadcast_default = json_int(o, "broadcast_default", s->broadcast_default);
    s->log_auto = json_int(o, "log_auto", s->log_auto);
    const char *ld = json_str(o, "log_dir", NULL);
    if (ld) {
        wchar_t *w = utf8_to_wide(ld);
        wcsncpy(s->log_dir, w, MAX_PATH - 1); s->log_dir[MAX_PATH - 1] = 0;
        free(w);
    }
    const char *dd = json_str(o, "download_dir", NULL);
    if (dd) {
        wchar_t *w = utf8_to_wide(dd);
        wcsncpy(s->download_dir, w, MAX_PATH - 1); s->download_dir[MAX_PATH - 1] = 0;
        free(w);
    }
    s->ui_lang = json_int(o, "ui_lang", s->ui_lang);
    s->ssh_keepalive = json_int(o, "ssh_keepalive", s->ssh_keepalive);
    s->double_click_copy = json_int(o, "double_click_copy", s->double_click_copy);
    s->middle_click_paste = json_int(o, "middle_click_paste", s->middle_click_paste);
    s->show_tab_close = json_int(o, "show_tab_close", s->show_tab_close);
    s->confirm_close = json_int(o, "confirm_close", s->confirm_close);
    s->smooth_scroll = json_int(o, "smooth_scroll", s->smooth_scroll);
    s->status_clock = json_int(o, "status_clock", s->status_clock);
    s->win_x = json_int(o, "win_x", -1);
    s->win_y = json_int(o, "win_y", -1);
    s->win_w = json_int(o, "win_w", -1);
    s->win_h = json_int(o, "win_h", -1);
    const char *hl = json_str(o, "highlights", NULL);
    if (hl) {
        strncpy(s->highlights, hl, sizeof(s->highlights) - 1);
        s->highlights[sizeof(s->highlights) - 1] = 0;
    }
}

BOOL settings_load(Settings *s) {
    settings_defaults(s);
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    yt_data_dir(dir, MAX_PATH);
    _snwprintf(path, MAX_PATH, L"%s\\settings.json", dir);
    path[MAX_PATH - 1] = 0;
    char *txt = yt_read_file(path, NULL);
    if (!txt) return FALSE;
    JVal *root = json_parse(txt);
    free(txt);
    if (!root || root->type != J_OBJ) { json_free(root); return FALSE; }
    json_to_settings(root, s);
    json_free(root);
    yt_mkdir(s->log_dir);
    return TRUE;
}

BOOL settings_save(Settings *s) {
    wchar_t dir[MAX_PATH], path[MAX_PATH], tmp[MAX_PATH];
    yt_data_dir(dir, MAX_PATH);
    _snwprintf(path, MAX_PATH, L"%s\\settings.json", dir);
    _snwprintf(tmp, MAX_PATH, L"%s\\settings.tmp", dir);
    path[MAX_PATH - 1] = 0; tmp[MAX_PATH - 1] = 0;
    JVal *root = json_new_obj();
    settings_to_json(root, s);
    char *txt = json_dump(root, 1);
    BOOL ok = yt_write_file(tmp, txt, strlen(txt));
    free(txt);
    json_free(root);
    if (!ok) return FALSE;
    MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* 会话                                                                 */
/* ------------------------------------------------------------------ */
const char *protocol_name(int proto) {
    switch (proto) {
    case PROTO_SSH: return "SSH";
    case PROTO_SERIAL: return "串口";
    case PROTO_TELNET: return "Telnet";
    case PROTO_LOCAL: return "本地";
    default: return "SSH";
    }
}
const char *protocol_key(int proto) {
    switch (proto) {
    case PROTO_SERIAL: return "serial";
    case PROTO_TELNET: return "telnet";
    case PROTO_LOCAL: return "local";
    default: return "ssh";
    }
}
int protocol_from_key(const char *key) {
    if (!key) return PROTO_SSH;
    if (strcmp(key, "serial") == 0) return PROTO_SERIAL;
    if (strcmp(key, "telnet") == 0) return PROTO_TELNET;
    if (strcmp(key, "local") == 0) return PROTO_LOCAL;
    return PROTO_SSH;
}

void session_generate_id(Session *s) {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    unsigned long long v = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    static int counter = 0;
    _snprintf(s->id, sizeof(s->id), "%08llx%02x%02x", v & 0xFFFFFFFFull, (unsigned)(counter++ & 0xFF), (unsigned)(GetTickCount() & 0xFF));
}

Session session_defaults(int protocol) {
    Session s;
    memset(&s, 0, sizeof(s));
    session_generate_id(&s);
    s.protocol = protocol;
    s.port = protocol == PROTO_SSH ? 22 : (protocol == PROTO_TELNET ? 23 : 0);
    strcpy(s.group, "默认");
    s.baud = 115200;
    s.databits = 8;
    s.parity = 0;
    s.stopbits = 1;
    s.dtr = 1;
    s.rts = 1;
    s.keepalive = 30;
    s.cols = 0;
    s.rows = 0;
    if (protocol == PROTO_SSH) {
        strcpy(s.name, "SSH 会话");
        strcpy(s.user, "root");
    } else if (protocol == PROTO_SERIAL) {
        strcpy(s.name, "串口会话");
        strcpy(s.port_name, "COM1");
    } else if (protocol == PROTO_TELNET) {
        strcpy(s.name, "Telnet 会话");
    } else {
        strcpy(s.name, "本地终端");
        strcpy(s.shell, "powershell.exe -NoLogo -NoProfile");
    }
    return s;
}

char *session_subtitle(Session *s, char *buf, int cch) {
    if (s->protocol == PROTO_SERIAL) {
        _snprintf(buf, (size_t)cch, "%s %d %d%c%d", s->port_name, s->baud, s->databits,
                  s->parity == 0 ? 'N' : (s->parity == 2 ? 'E' : 'O'), s->stopbits);
    } else if (s->protocol == PROTO_LOCAL) {
        char tmp[128];
        wchar_t w[512];
        wchar_t *ws = utf8_to_wide(s->shell);
        yt_basename(ws, w, 512);
        char *b = wide_to_utf8(w);
        strncpy(tmp, b, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = 0;
        free(b); free(ws);
        _snprintf(buf, (size_t)cch, "%s", tmp);
    } else {
        _snprintf(buf, (size_t)cch, "%s%s@%s:%d", "", s->user, s->host, s->port);
    }
    buf[cch - 1] = 0;
    return buf;
}

void sessions_init(SessionList *l) { l->items = NULL; l->n = l->cap = 0; }

void sessions_free(SessionList *l) { free(l->items); l->items = NULL; l->n = l->cap = 0; }

Session *sessions_add(SessionList *l, const Session *src) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->items = (Session *)xrealloc(l->items, (size_t)l->cap * sizeof(Session));
    }
    l->items[l->n] = *src;
    if (!l->items[l->n].id[0]) session_generate_id(&l->items[l->n]);
    return &l->items[l->n++];
}

void sessions_remove(SessionList *l, int index) {
    if (index < 0 || index >= l->n) return;
    memmove(&l->items[index], &l->items[index + 1], (size_t)(l->n - index - 1) * sizeof(Session));
    l->n--;
}

Session *sessions_find(SessionList *l, const char *id) {
    int i = sessions_find_index(l, id);
    return i < 0 ? NULL : &l->items[i];
}
int sessions_find_index(SessionList *l, const char *id) {
    if (!id) return -1;
    for (int i = 0; i < l->n; i++) if (strcmp(l->items[i].id, id) == 0) return i;
    return -1;
}

static void session_to_json(JVal *o, Session *s) {
    json_set_str(o, "id", s->id);
    json_set_str(o, "name", s->name);
    json_set_str(o, "group", s->group);
    json_set_str(o, "protocol", protocol_key(s->protocol));
    json_set_int(o, "favorite", s->favorite);
    json_set_str(o, "note", s->note);
    json_set_str(o, "host", s->host);
    json_set_int(o, "port", s->port);
    json_set_str(o, "user", s->user);
    json_set_int(o, "auth", s->auth);
    json_set_str(o, "keyfile", s->keyfile);
    json_set_int(o, "keepalive", s->keepalive);
    json_set_int(o, "compression", s->compression);
    json_set_str(o, "jump", s->jump);
    json_set_str(o, "extra_args", s->extra_args);
    json_set_str(o, "tunnels", s->tunnels);
    json_set_str(o, "port_name", s->port_name);
    json_set_int(o, "baud", s->baud);
    json_set_int(o, "databits", s->databits);
    json_set_int(o, "parity", s->parity);
    json_set_int(o, "stopbits", s->stopbits);
    json_set_int(o, "flow", s->flow);
    json_set_int(o, "dtr", s->dtr);
    json_set_int(o, "rts", s->rts);
    json_set_int(o, "local_echo", s->local_echo);
    json_set_str(o, "shell", s->shell);
    json_set_str(o, "quick_cmds", s->quick_cmds);
    json_set_str(o, "triggers", s->triggers);
    const char *cwd_u = (const char *)wide_to_utf8(s->cwd);
    json_set_str(o, "cwd", cwd_u ? cwd_u : "");
    free((void *)cwd_u);
    json_set_int(o, "cols", s->cols);
    json_set_int(o, "rows", s->rows);
    json_set_int(o, "encoding", s->encoding);
    char *enc = NULL;
    if (s->password[0]) enc = yt_dpapi_encrypt_hex(s->password);
    json_set_str(o, "password_enc", enc ? enc : s->password_enc);
    free(enc);
}

static void json_to_session(JVal *o, Session *s) {
    memset(s, 0, sizeof(Session));
    const char *v;
#define GETSTR(field, key) v = json_str(o, key, ""); if (v) { strncpy(s->field, v, sizeof(s->field) - 1); }
    GETSTR(id, "id")
    GETSTR(name, "name")
    GETSTR(group, "group")
    GETSTR(note, "note")
    s->protocol = protocol_from_key(json_str(o, "protocol", "ssh"));
    s->favorite = json_int(o, "favorite", 0);
    GETSTR(host, "host")
    s->port = json_int(o, "port", 22);
    GETSTR(user, "user")
    s->auth = json_int(o, "auth", 0);
    GETSTR(keyfile, "keyfile")
    s->keepalive = json_int(o, "keepalive", 30);
    s->compression = json_int(o, "compression", 0);
    GETSTR(jump, "jump")
    GETSTR(extra_args, "extra_args")
    GETSTR(tunnels, "tunnels")
    GETSTR(port_name, "port_name")
    s->baud = json_int(o, "baud", 115200);
    s->databits = json_int(o, "databits", 8);
    s->parity = json_int(o, "parity", 0);
    s->stopbits = json_int(o, "stopbits", 1);
    s->flow = json_int(o, "flow", 0);
    s->dtr = json_int(o, "dtr", 1);
    s->rts = json_int(o, "rts", 1);
    s->local_echo = json_int(o, "local_echo", 0);
    GETSTR(shell, "shell")
    GETSTR(quick_cmds, "quick_cmds")
    GETSTR(triggers, "triggers")
    v = json_str(o, "cwd", "");
    if (v && *v) {
        wchar_t *w = utf8_to_wide(v);
        wcsncpy(s->cwd, w, MAX_PATH * 2 - 1);
        free(w);
    }
    s->cols = json_int(o, "cols", 0);
    s->rows = json_int(o, "rows", 0);
    s->encoding = json_int(o, "encoding", 0);
    GETSTR(password_enc, "password_enc")
    if (s->password_enc[0]) {
        char *plain = yt_dpapi_decrypt_hex(s->password_enc);
        if (plain) {
            strncpy(s->password, plain, sizeof(s->password) - 1);
            free(plain);
        }
    }
#undef GETSTR
    if (!s->id[0]) session_generate_id(s);
    if (!s->group[0]) strcpy(s->group, "默认");
}

BOOL sessions_load(SessionList *list) {
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    yt_data_dir(dir, MAX_PATH);
    _snwprintf(path, MAX_PATH, L"%s\\sessions.json", dir);
    path[MAX_PATH - 1] = 0;
    char *txt = yt_read_file(path, NULL);
    if (!txt) return FALSE;
    JVal *root = json_parse(txt);
    free(txt);
    if (!root || root->type != J_OBJ) { json_free(root); return FALSE; }
    JVal *arr = json_get(root, "sessions");
    if (arr && arr->type == J_ARR) {
        for (int i = 0; i < arr->nitems; i++) {
            if (arr->items[i]->type != J_OBJ) continue;
            Session s;
            json_to_session(arr->items[i], &s);
            sessions_add(list, &s);
        }
    }
    json_free(root);
    return TRUE;
}

BOOL sessions_save(SessionList *list) {
    wchar_t dir[MAX_PATH], path[MAX_PATH], tmp[MAX_PATH];
    yt_data_dir(dir, MAX_PATH);
    _snwprintf(path, MAX_PATH, L"%s\\sessions.json", dir);
    _snwprintf(tmp, MAX_PATH, L"%s\\sessions.tmp", dir);
    path[MAX_PATH - 1] = 0; tmp[MAX_PATH - 1] = 0;
    JVal *root = json_new_obj();
    json_set_int(root, "version", 1);
    JVal *arr = json_new_arr();
    for (int i = 0; i < list->n; i++) {
        JVal *o = json_new_obj();
        session_to_json(o, &list->items[i]);
        json_add(arr, o);
    }
    json_set(root, "sessions", arr);
    char *txt = json_dump(root, 1);
    BOOL ok = yt_write_file(tmp, txt, strlen(txt));
    free(txt);
    json_free(root);
    if (!ok) return FALSE;
    MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    return TRUE;
}

