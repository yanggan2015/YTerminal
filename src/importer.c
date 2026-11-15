#include "app.h"
#include "ids.h"
#include <commdlg.h>
#include <shlobj.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* 通用：把会话写入列表（去重）                                          */
/* ================================================================== */
static int g_imported = 0;

static int dup_session(Session *s) {
    for (int i = 0; i < g_app.sessions.n; i++) {
        Session *o = &g_app.sessions.items[i];
        if (o->protocol != s->protocol) continue;
        if (o->protocol == PROTO_SERIAL) {
            if (_stricmp(o->port_name, s->port_name) == 0) return 1;
        } else if (_stricmp(o->host, s->host) == 0 && _stricmp(o->user, s->user) == 0 && o->port == s->port) {
            return 1;
        }
    }
    return 0;
}

static void add_imported(Session *s) {
    if (!s->host[0] && !s->port_name[0]) return;
    if (dup_session(s)) return;
    session_generate_id(s);
    sessions_add(&g_app.sessions, s);
    g_imported++;
}

/* ================================================================== */
/* SecureCRT .ini                                                       */
/* ================================================================== */
static int parse_securecrt_file(const wchar_t *path) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return 0;
    char host[256] = { 0 }, user[96] = { 0 }, proto[64] = { 0 }, pass[256] = { 0 }, key[512] = { 0 };
    int port = 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!strncmp(p, "S:\"Hostname\"=", 14)) {
            char *v = p + 14; yt_trim(v);
            if (*v == '"') { v++; char *e = strrchr(v, '"'); if (e) *e = 0; }
            strncpy(host, v, sizeof(host) - 1);
        } else if (!strncmp(p, "S:\"Username\"=", 14)) {
            char *v = p + 14; yt_trim(v);
            if (*v == '"') { v++; char *e = strrchr(v, '"'); if (e) *e = 0; }
            strncpy(user, v, sizeof(user) - 1);
        } else if (!strncmp(p, "D:\"[SSH2] Port\"=", 17)) {
            char *v = p + 17; yt_trim(v);
            port = (int)strtol(v, NULL, 16);
        } else if (!strncmp(p, "S:\"Protocol Name\"=", 19)) {
            char *v = p + 19; yt_trim(v);
            if (*v == '"') { v++; char *e = strrchr(v, '"'); if (e) *e = 0; }
            strncpy(proto, v, sizeof(proto) - 1);
        } else if (!strncmp(p, "S:\"Password\"=", 15)) {
            char *v = p + 15; yt_trim(v);
            if (*v == '"') { v++; char *e = strrchr(v, '"'); if (e) *e = 0; }
            strncpy(pass, v, sizeof(pass) - 1);
        } else if (!strncmp(p, "S:\"Identity File\"=", 20)) {
            char *v = p + 20; yt_trim(v);
            if (*v == '"') { v++; char *e = strrchr(v, '"'); if (e) *e = 0; }
            strncpy(key, v, sizeof(key) - 1);
        }
    }
    fclose(f);
    if (!host[0] && !user[0]) return 0;

    Session s = session_defaults(PROTO_SSH);
    if (proto[0] && _stricmp(proto, "telnet") == 0) s.protocol = PROTO_TELNET;
    strncpy(s.host, host, sizeof(s.host) - 1);
    strncpy(s.user, user, sizeof(s.user) - 1);
    s.port = port > 0 ? port : (s.protocol == PROTO_TELNET ? 23 : 22);
    strncpy(s.password, pass, sizeof(s.password) - 1);
    if (key[0]) { s.auth = AUTH_KEY; strncpy(s.keyfile, key, sizeof(s.keyfile) - 1); }
    const wchar_t *base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;
    char *nm = wide_to_utf8(base);
    if (nm) {
        size_t n = strlen(nm);
        if (n > 4 && _stricmp(nm + n - 4, ".ini") == 0) nm[n - 4] = 0;
        strncpy(s.name, nm, sizeof(s.name) - 1);
        free(nm);
    }
    strncpy(s.group, "SecureCRT", sizeof(s.group) - 1);
    add_imported(&s);
    return 1;
}

static int scan_dir_ini(const wchar_t *dir, int depth) {
    if (depth > 6) return 0;
    wchar_t pat[MAX_PATH * 2];
    _snwprintf(pat, MAX_PATH * 2, L"%s\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int count = 0;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        wchar_t full[MAX_PATH * 2];
        _snwprintf(full, MAX_PATH * 2, L"%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            count += scan_dir_ini(full, depth + 1);
        } else {
            size_t n = wcslen(fd.cFileName);
            if (n > 4 && _wcsicmp(fd.cFileName + n - 4, L".ini") == 0) {
                if (parse_securecrt_file(full)) count++;
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return count;
}

static void import_securecrt(HWND owner) {
    wchar_t path[MAX_PATH * 2] = { 0 };
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"SecureCRT 会话 (*.ini)\0*.ini\0所有文件\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) { g_imported = 0; parse_securecrt_file(path); }
}

/* ================================================================== */
/* MobaXterm .ini                                                       */
/* ================================================================== */
static int parse_moba_bookmarks(const wchar_t *path) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return 0;
    char line[2048];
    int in_bookmark = 0, count = 0;
    while (fgets(line, sizeof(line), f)) {
        yt_trim(line);
        if (line[0] == '[') {
            in_bookmark = (_stricmp(line, "[Bookmarks]") == 0);
            continue;
        }
        if (!in_bookmark || !line[0]) continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = line, *v = eq + 1;
        yt_trim(k); yt_trim(v);
        if (_strnicmp(k, "host", 4) != 0 && _strnicmp(k, "serial", 6) != 0) continue;
        Session s;
        if (_strnicmp(k, "serial", 6) == 0) {
            s = session_defaults(PROTO_SERIAL);
            char port[32] = { 0 };
            int baud = 115200;
            sscanf(v, "%31[^:]:%d", port, &baud);
            strncpy(s.port_name, port, sizeof(s.port_name) - 1);
            s.baud = baud;
            strncpy(s.name, port, sizeof(s.name) - 1);
            strncpy(s.group, "MobaXterm", sizeof(s.group) - 1);
        } else {
            s = session_defaults(PROTO_SSH);
            char tmp[512];
            strncpy(tmp, v, sizeof(tmp) - 1);
            char *at = strchr(tmp, '@');
            char *user = tmp, *host = NULL;
            if (at) { *at = 0; host = at + 1; }
            else host = tmp;
            int port = 22;
            char *colon = strrchr(host, ':');
            if (colon) { *colon = 0; port = atoi(colon + 1); }
            if (at) strncpy(s.user, user, sizeof(s.user) - 1);
            strncpy(s.host, host, sizeof(s.host) - 1);
            s.port = port;
            _snprintf(s.name, sizeof(s.name), "%s@%s", s.user, s.host);
            strncpy(s.group, "MobaXterm", sizeof(s.group) - 1);
        }
        add_imported(&s);
        count++;
    }
    fclose(f);
    return count;
}

static void import_moba(HWND owner) {
    wchar_t path[MAX_PATH * 2] = { 0 };
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"MobaXterm 配置 (*.ini)\0*.ini\0所有文件\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) { g_imported = 0; parse_moba_bookmarks(path); }
}

/* ================================================================== */
/* PuTTY 注册表                                                         */
/* ================================================================== */
static void url_decode_inplace(char *s) {
    char out[512];
    int j = 0;
    for (int i = 0; s[i] && j < 511; i++) {
        if (s[i] == '%' && s[i + 1] && s[i + 2]) {
            char h[3] = { s[i + 1], s[i + 2], 0 };
            out[j++] = (char)strtol(h, NULL, 16);
            i += 2;
        } else out[j++] = s[i];
    }
    out[j] = 0;
    strcpy(s, out);
}

static int import_putty_reg(void) {
    HKEY root;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\SimonTatham\\PuTTY\\Sessions", 0, KEY_READ, &root) != ERROR_SUCCESS)
        return 0;
    int count = 0;
    for (DWORD i = 0;; i++) {
        wchar_t name[256];
        DWORD nlen = 256;
        if (RegEnumKeyExW(root, i, name, &nlen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        HKEY k;
        if (RegOpenKeyExW(root, name, 0, KEY_READ, &k) != ERROR_SUCCESS) continue;
        char host[256] = { 0 }, user[96] = { 0 }, key[512] = { 0 };
        DWORD port = 22, proto = 3;
        wchar_t wv[512];
        DWORD sz = sizeof(wv), type;
        if (RegQueryValueExW(k, L"HostName", NULL, &type, (BYTE *)wv, &sz) == ERROR_SUCCESS) {
            char *u = wide_to_utf8(wv); if (u) { strncpy(host, u, sizeof(host) - 1); free(u); }
        }
        sz = sizeof(wv);
        if (RegQueryValueExW(k, L"UserName", NULL, &type, (BYTE *)wv, &sz) == ERROR_SUCCESS) {
            char *u = wide_to_utf8(wv); if (u) { strncpy(user, u, sizeof(user) - 1); free(u); }
        }
        sz = sizeof(wv);
        if (RegQueryValueExW(k, L"PublicKeyFile", NULL, &type, (BYTE *)wv, &sz) == ERROR_SUCCESS) {
            char *u = wide_to_utf8(wv); if (u) { strncpy(key, u, sizeof(key) - 1); free(u); }
        }
        sz = sizeof(DWORD);
        RegQueryValueExW(k, L"PortNumber", NULL, &type, (BYTE *)&port, &sz);
        sz = sizeof(DWORD);
        RegQueryValueExW(k, L"Protocol", NULL, &type, (BYTE *)&proto, &sz);
        RegCloseKey(k);
        if (!host[0]) continue;
        char sname[256];
        char *sn = wide_to_utf8(name);
        strncpy(sname, sn ? sn : "", 255);
        sname[255] = 0;
        free(sn);
        url_decode_inplace(sname);
        Session s = session_defaults(proto == 1 ? PROTO_TELNET : PROTO_SSH);
        strncpy(s.host, host, sizeof(s.host) - 1);
        strncpy(s.user, user, sizeof(s.user) - 1);
        s.port = (int)port;
        strncpy(s.name, sname, sizeof(s.name) - 1);
        strncpy(s.group, "PuTTY", sizeof(s.group) - 1);
        if (key[0]) { s.auth = AUTH_KEY; strncpy(s.keyfile, key, sizeof(s.keyfile) - 1); }
        add_imported(&s);
        count++;
    }
    RegCloseKey(root);
    return count;
}

/* ================================================================== */
/* OpenSSH ~/.ssh/config                                                */
/* ================================================================== */
static void expand_tilde(const char *in, char *out, int cch) {
    if (in && in[0] == '~') {
        const wchar_t *up = _wgetenv(L"USERPROFILE");
        if (!up) up = L"";
        char *u8 = wide_to_utf8(up);
        _snprintf(out, cch, "%s%s", u8 ? u8 : "", in + 1);
        out[cch - 1] = 0;
        free(u8);
    } else {
        _snprintf(out, cch, "%s", in ? in : "");
        out[cch - 1] = 0;
    }
}

static int is_wildcard(const char *s) {
    return strchr(s, '*') != NULL || strchr(s, '?') != NULL;
}

static int parse_ssh_config_file(const wchar_t *path) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return 0;
    Session cur;
    memset(&cur, 0, sizeof(cur));
    int have = 0, count = 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        yt_trim(p);
        if (!*p || *p == '#') continue;
        char key[64] = { 0 }, val[512] = { 0 };
        char *sp = p;
        while (*sp && *sp != ' ' && *sp != '\t' && *sp != '=') sp++;
        size_t klen = (size_t)(sp - p);
        if (klen >= sizeof(key)) klen = sizeof(key) - 1;
        memcpy(key, p, klen); key[klen] = 0;
        while (*sp == ' ' || *sp == '\t' || *sp == '=') sp++;
        strncpy(val, sp, sizeof(val) - 1);
        yt_trim(val);

        if (_stricmp(key, "Host") == 0) {
            if (have) { add_imported(&cur); count++; }
            char first[128] = { 0 };
            sscanf(val, "%127s", first);
            if (!first[0]) { have = 0; continue; }
            cur = session_defaults(PROTO_SSH);
            strncpy(cur.group, "OpenSSH", sizeof(cur.group) - 1);
            strncpy(cur.name, first, sizeof(cur.name) - 1);
            strncpy(cur.host, first, sizeof(cur.host) - 1);
            have = !is_wildcard(first) && _stricmp(first, "*") != 0;
            continue;
        }
        if (!have) continue;
        if (_stricmp(key, "HostName") == 0) {
            char tmp[256];
            expand_tilde(val, tmp, sizeof(tmp));
            strncpy(cur.host, tmp, sizeof(cur.host) - 1);
        } else if (_stricmp(key, "User") == 0) {
            strncpy(cur.user, val, sizeof(cur.user) - 1);
        } else if (_stricmp(key, "Port") == 0) {
            int pt = atoi(val);
            if (pt > 0) cur.port = pt;
        } else if (_stricmp(key, "IdentityFile") == 0) {
            char tmp[MAX_PATH * 2];
            expand_tilde(val, tmp, sizeof(tmp));
            cur.auth = AUTH_KEY;
            strncpy(cur.keyfile, tmp, sizeof(cur.keyfile) - 1);
        } else if (_stricmp(key, "ProxyJump") == 0) {
            if (_stricmp(val, "none") != 0) strncpy(cur.jump, val, sizeof(cur.jump) - 1);
        } else if (_stricmp(key, "UserKnownHostsFile") == 0) {
            /* 忽略 */
        }
    }
    if (have) { add_imported(&cur); count++; }
    fclose(f);
    return count;
}

static int import_ssh_config(void) {
    const wchar_t *up = _wgetenv(L"USERPROFILE");
    if (!up) return 0;
    wchar_t path[MAX_PATH * 2];
    _snwprintf(path, MAX_PATH * 2, L"%s\\.ssh\\config", up);
    return parse_ssh_config_file(path);
}

static void import_ssh_config_dialog(HWND owner) {
    wchar_t path[MAX_PATH * 2] = { 0 };
    const wchar_t *up = _wgetenv(L"USERPROFILE");
    if (up) _snwprintf(path, MAX_PATH * 2, L"%s\\.ssh\\config", up);
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"OpenSSH 配置 (config)\0config\0所有文件\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) { g_imported = 0; parse_ssh_config_file(path); }
}

/* ================================================================== */
/* 自动扫描                                                             */
/* ================================================================== */
static void auto_scan(void) {
    g_imported = 0;
    wchar_t dir[MAX_PATH * 2];
    const wchar_t *appdata = _wgetenv(L"APPDATA");
    if (!appdata) appdata = L"";
    const wchar_t *userprofile = _wgetenv(L"USERPROFILE");
    if (!userprofile) userprofile = L"";

    _snwprintf(dir, MAX_PATH * 2, L"%s\\VanDyke\\Config\\Sessions", appdata);
    scan_dir_ini(dir, 0);

    _snwprintf(dir, MAX_PATH * 2, L"%s\\MobaXterm\\MobaXterm.ini", appdata);
    parse_moba_bookmarks(dir);
    _snwprintf(dir, MAX_PATH * 2, L"%s\\Documents\\MobaXterm\\MobaXterm.ini", userprofile);
    parse_moba_bookmarks(dir);

    import_putty_reg();
    import_ssh_config();
}

void app_import_sessions(HWND parent) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"从 SecureCRT 导入 (.ini)…");
    AppendMenuW(m, MF_STRING, 2, L"从 MobaXterm 导入 (.ini)…");
    AppendMenuW(m, MF_STRING, 3, L"从 PuTTY 导入 (注册表)");
    AppendMenuW(m, MF_STRING, 5, L"从 OpenSSH (~/.ssh/config) 导入");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, 4, L"自动扫描并全部导入");
    POINT pt;
    GetCursorPos(&pt);
    if (parent && IsWindowVisible(parent)) {
        RECT rc; GetWindowRect(parent, &rc);
        pt.x = rc.left + 60;
        pt.y = rc.top + 80;
    }
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN, pt.x, pt.y, 0, parent, NULL);
    DestroyMenu(m);
    if (cmd == 1) import_securecrt(parent);
    else if (cmd == 2) import_moba(parent);
    else if (cmd == 3) { g_imported = 0; import_putty_reg(); }
    else if (cmd == 5) { g_imported = 0; import_ssh_config_dialog(parent); }
    else if (cmd == 4) auto_scan();
    else return;

    sessions_save(&g_app.sessions);
    PostMessageW(g_app.main, WM_YT_SIDEBAR_REFRESH, 0, 0);
    wchar_t msg[256];
    _snwprintf(msg, 256, L"导入完成：新增 %d 个会话（已自动跳过重复项）。", g_imported);
    MessageBoxW(parent, msg, L"YTerminal 导入", MB_OK | MB_ICONINFORMATION);
}
