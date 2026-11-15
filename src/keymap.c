/* YTerminal - keymap.c : 快捷键表、解析、冲突检测与序列化 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "ids.h"
#include "keymap.h"

static const YtKeyDef g_defs[] = {
    { IDC_COPY,      L"复制选区",         "Ctrl+Shift+C" },
    { IDC_PASTE,     L"粘贴",             "Ctrl+Shift+V" },
    { IDC_FIND,      L"查找文本",         "Ctrl+Shift+F" },
    { IDC_SELECTALL, L"全选",             "Ctrl+Shift+A" },
    { IDC_LOG,       L"日志记录开关",      "Ctrl+Shift+L" },
    { IDC_SPLIT_V,   L"上下分屏",         "Ctrl+Shift+D" },
    { IDC_SPLIT_H,   L"左右分屏",         "Ctrl+Shift+E" },
    { IDC_QUICKCMD,  L"快捷命令与规则",    "Ctrl+Shift+K" },
    { IDC_SFTP,      L"SFTP 传输",        "Ctrl+Shift+S" },
    { IDC_PALETTE,   L"命令面板",         "Ctrl+Shift+P" },
    { IDC_CLOSE_TAB, L"关闭标签",         "Ctrl+W" },
    { IDC_NEW_TAB,   L"新建标签",         "Ctrl+Tab" },
    { IDC_FONT_INC,  L"放大字号",         "Ctrl+=" },
    { IDC_FONT_DEC,  L"缩小字号",         "Ctrl+-" },
    { IDC_FONT_RESET,L"重置字号",         "Ctrl+0" },
    { IDC_SETTINGS,  L"设置",             "Ctrl+," },
    { IDC_SHORTCUTS, L"快捷键一览",        "Ctrl+/" },
    { IDC_TUNNELS,   L"端口转发管理器",    "Ctrl+Shift+T" },
    { IDC_BROADCAST, L"广播输入",         "Ctrl+Shift+B" },
    { IDC_KEYMAP,    L"快捷键设置",        "Ctrl+Shift+M" },
};

#define KM_N ((int)(sizeof(g_defs) / sizeof(g_defs[0])))
#define KM_SPEC 32

static char g_spec[KM_N][KM_SPEC];
static int  g_mods[KM_N];
static int  g_vk[KM_N];
static char g_dump[KM_N * (KM_SPEC + 8)];
static int  g_ready = 0;

static void km_ensure_init(void);

static int km_streq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

typedef struct { const char *name; int vk; } KeyName;
static const KeyName g_names[] = {
    { "Tab", VK_TAB }, { "Enter", VK_RETURN }, { "Esc", VK_ESCAPE }, { "Escape", VK_ESCAPE },
    { "Space", VK_SPACE }, { "Backspace", VK_BACK }, { "Insert", VK_INSERT },
    { "Delete", VK_DELETE }, { "Del", VK_DELETE }, { "Home", VK_HOME }, { "End", VK_END },
    { "PgUp", VK_PRIOR }, { "PageUp", VK_PRIOR }, { "PgDn", VK_NEXT }, { "PageDown", VK_NEXT },
    { "Left", VK_LEFT }, { "Right", VK_RIGHT }, { "Up", VK_UP }, { "Down", VK_DOWN },
    { "=", VK_OEM_PLUS }, { "+", VK_OEM_PLUS }, { "-", VK_OEM_MINUS }, { ",", VK_OEM_COMMA },
    { ".", VK_OEM_PERIOD }, { "/", VK_OEM_2 }, { ";", VK_OEM_1 }, { "'", VK_OEM_7 },
    { "[", VK_OEM_4 }, { "]", VK_OEM_6 }, { "\\", VK_OEM_5 }, { "`", VK_OEM_3 },
};
#define KN_N ((int)(sizeof(g_names) / sizeof(g_names[0])))

/* 单字符 → 虚拟键 */
static int km_char_vk(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 'A' && c <= 'Z') return c;
    if (c >= '0' && c <= '9') return c;
    for (int i = 0; i < KN_N; i++)
        if (g_names[i].name[1] == 0 && g_names[i].name[0] == c) return g_names[i].vk;
    return 0;
}

/* F1..F24 */
static int km_fkey(const char *t) {
    if ((t[0] != 'F' && t[0] != 'f') || !t[1]) return 0;
    int n = atoi(t + 1);
    if (n < 1 || n > 24) return 0;
    return VK_F1 + (n - 1);
}

static int km_name_vk(const char *t) {
    if (!t || !*t) return 0;
    if (t[1] == 0) {
        int vk = km_char_vk(t[0]);
        if (vk) return vk;
    }
    for (int i = 0; i < KN_N; i++)
        if (km_streq(t, g_names[i].name)) return g_names[i].vk;
    return km_fkey(t);
}

static const char *km_vk_name(int vk) {
    static char buf[8];
    if (vk >= 'A' && vk <= 'Z') { buf[0] = (char)vk; buf[1] = 0; return buf; }
    if (vk >= '0' && vk <= '9') { buf[0] = (char)vk; buf[1] = 0; return buf; }
    if (vk >= VK_F1 && vk <= VK_F24) {
        int n = vk - VK_F1 + 1;
        _snprintf(buf, sizeof(buf), "F%d", n);
        buf[sizeof(buf) - 1] = 0;
        return buf;
    }
    for (int i = 0; i < KN_N; i++)
        if (g_names[i].vk == vk) return g_names[i].name;
    return NULL;
}

int yt_key_parse(const char *spec, int *mods, int *vk) {
    char tmp[64];
    if (!spec || !*spec) return -1;
    if (strlen(spec) >= sizeof(tmp)) return -1;
    strcpy(tmp, spec);
    int m = 0;
    char *p = tmp;
    while (p && *p) {
        char *plus = strchr(p, '+');
        if (plus) *plus = 0;
        while (*p == ' ') p++;
        char *e = p + strlen(p);
        while (e > p && (e[-1] == ' ' || e[-1] == '\t')) e--;
        *e = 0;
        if (!*p) return -1;
        if (plus) {
            if (km_streq(p, "Ctrl") || km_streq(p, "Control")) m |= YT_KM_CTRL;
            else if (km_streq(p, "Shift")) m |= YT_KM_SHIFT;
            else if (km_streq(p, "Alt")) m |= YT_KM_ALT;
            else if (km_streq(p, "Win")) m |= 8;
            else return -1;
            p = plus + 1;
        } else {
            int code = km_name_vk(p);
            if (!code) return -1;
            if (mods) *mods = m;
            if (vk) *vk = code;
            return 0;
        }
    }
    return -1;
}

void yt_key_format(int mods, int vk, char *out, int cap) {
    const char *kn = km_vk_name(vk);
    if (!out || cap <= 0) return;
    if (!kn) { out[0] = 0; return; }
    _snprintf(out, (size_t)cap, "%s%s%s%s", (mods & YT_KM_CTRL) ? "Ctrl+" : "",
              (mods & YT_KM_SHIFT) ? "Shift+" : "", (mods & YT_KM_ALT) ? "Alt+" : "", kn);
    out[cap - 1] = 0;
}

int yt_key_count(void) { return KM_N; }

const YtKeyDef *yt_key_def(int i) {
    if (i < 0 || i >= KM_N) return NULL;
    return &g_defs[i];
}

const char *yt_key_spec(int i) {
    if (i < 0 || i >= KM_N) return "";
    return g_spec[i];
}

int yt_key_cmd(int i) {
    if (i < 0 || i >= KM_N) return 0;
    return g_defs[i].action;
}

void yt_key_display(int i, wchar_t *out, int cap) {
    km_ensure_init();
    if (!out || cap <= 0) return;
    const char *spec = yt_key_spec(i);
    if (!spec[0]) { wcsncpy(out, L"未绑定", (size_t)cap); out[cap - 1] = 0; return; }
    char tmp[64];
    int m = 0, vk = 0;
    if (yt_key_parse(spec, &m, &vk) == 0) yt_key_format(m, vk, tmp, sizeof(tmp));
    else strcpy(tmp, spec);
    MultiByteToWideChar(CP_UTF8, 0, tmp, -1, out, cap);
    out[cap - 1] = 0;
}

int yt_key_conflicts(void) {
    km_ensure_init();
    int n = 0;
    for (int i = 0; i < KM_N; i++) {
        if (!g_spec[i][0]) continue;
        for (int j = i + 1; j < KM_N; j++) {
            if (!g_spec[j][0]) continue;
            if (g_mods[i] == g_mods[j] && g_vk[i] == g_vk[j]) n++;
        }
    }
    return n;
}

/* 内部：解析并存入 i，冲突则返回冲突索引（-1 表示无） */
static int km_apply(int i, const char *spec, int *parsed_ok) {
    int m = 0, vk = 0;
    if (parsed_ok) *parsed_ok = 0;
    if (!spec[0]) {
        g_spec[i][0] = 0; g_mods[i] = 0; g_vk[i] = 0;
        if (parsed_ok) *parsed_ok = 1;
        return -1;
    }
    if (yt_key_parse(spec, &m, &vk) != 0) return -1;
    int hit = -1;
    if (parsed_ok) *parsed_ok = 1;
    for (int j = 0; j < KM_N; j++) {
        if (j == i || !g_spec[j][0]) continue;
        if (g_mods[j] == m && g_vk[j] == vk) { hit = j; break; }
    }
    if (hit >= 0) return hit;
    strncpy(g_spec[i], spec, KM_SPEC - 1);
    g_spec[i][KM_SPEC - 1] = 0;
    g_mods[i] = m; g_vk[i] = vk;
    if (parsed_ok) *parsed_ok = 1;
    return -1;
}

int yt_key_bind(int i, const char *spec, int *conflict) {
    if (i < 0 || i >= KM_N || !spec) return -1;
    if (conflict) *conflict = -1;
    int ok = 0;
    int hit = km_apply(i, spec, &ok);
    if (!ok) return -1;
    if (hit >= 0) { if (conflict) *conflict = hit; return -2; }
    return 0;
}

void yt_key_unbind(int i) {
    if (i < 0 || i >= KM_N) return;
    g_spec[i][0] = 0; g_mods[i] = 0; g_vk[i] = 0;
}

void yt_key_reset(int i) {
    if (i < 0 || i >= KM_N) return;
    int ok = 0;
    km_apply(i, g_defs[i].def, &ok);
}

void yt_key_reset_all(void) {
    g_ready = 1;
    for (int i = 0; i < KM_N; i++) yt_key_reset(i);
}

int yt_key_lookup(int ctrl, int shift, int alt, int vk) {
    km_ensure_init();
    int m = (ctrl ? YT_KM_CTRL : 0) | (shift ? YT_KM_SHIFT : 0) | (alt ? YT_KM_ALT : 0);
    for (int i = 0; i < KM_N; i++)
        if (g_spec[i][0] && g_mods[i] == m && g_vk[i] == vk) return g_defs[i].action;
    return 0;
}

const char *yt_key_dump(void) {
    km_ensure_init();
    g_dump[0] = 0;
    size_t used = 0;
    for (int i = 0; i < KM_N; i++) {
        size_t need = strlen(g_spec[i]) + 8;
        if (used + need >= sizeof(g_dump)) break;
        _snprintf(g_dump + used, sizeof(g_dump) - used, "%s%d=%s", i ? ";" : "", i, g_spec[i]);
        used += strlen(g_dump + used);
    }
    return g_dump;
}

int yt_key_load(const char *blob) {
    int applied = 0;
    yt_key_reset_all();
    if (!blob || !*blob) return 0;
    char tmp[1024];
    strncpy(tmp, blob, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    char *save = NULL;
    for (char *tok = strtok_r(tmp, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        int idx = atoi(tok);
        if (idx < 0 || idx >= KM_N) continue;
        int ok = 0;
        if (km_apply(idx, eq + 1, &ok) < 0 && ok) applied++;
    }
    return applied;
}

/* 模块静态数组零初始化，首次访问前需要灌默认值。
   这里用惰性初始化，避免依赖 CRT 构造函数。 */
static void km_ensure_init(void) {
    if (g_ready) return;
    g_ready = 1;
    yt_key_reset_all();
}

void yt_key_init(void) {
    g_ready = 1;
    yt_key_reset_all();
}
