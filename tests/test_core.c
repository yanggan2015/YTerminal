/* YTerminal - tests/test_core.c : 纯逻辑单元测试（不依赖 Win32 消息循环） */
#include "util.h"
#include "core.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int g_pass = 0, g_fail = 0;

static void check(const char *name, int cond) {
    if (cond) { g_pass++; printf("  [PASS] %s\n", name); }
    else { g_fail++; printf("  [FAIL] %s\n", name); }
}

int main(void) {
    printf("=== YTerminal core tests ===\n");

    /* 模糊匹配 */
    check("fuzzy empty needle matches", yt_fuzzy_score("", "Settings") > 0);
    check("fuzzy exact prefix scores high", yt_fuzzy_score("set", "Settings") > yt_fuzzy_score("stg", "Settings"));
    check("fuzzy subsequence matches", yt_fuzzy_score("sfp", "SFTP 窗口") > 0);
    check("fuzzy mismatch returns 0", yt_fuzzy_score("xyz", "SFTP 窗口") == 0);
    check("fuzzy case insensitive", yt_fuzzy_score("SET", "settings") > 0);
    check("fuzzy space tolerated", yt_fuzzy_score("new tab", "新建标签 NewTab") > 0);

    /* 转发规格 */
    YtFwd f;
    check("parse L 3 fields", yt_fwd_parse("L:8080:localhost:80", &f) && f.type == 0 &&
          strcmp(f.listen, "8080") == 0 && strcmp(f.host, "localhost") == 0 && strcmp(f.dest, "80") == 0);
    check("parse R 4 fields", yt_fwd_parse("R:127.0.0.1:9000:db:5432", &f) && f.type == 1 &&
          strcmp(f.listen, "127.0.0.1:9000") == 0);
    check("parse D form", yt_fwd_parse("D:1080", &f) && f.type == 2 && strcmp(f.listen, "1080") == 0 &&
          f.host[0] == 0);
    check("parse lowercase prefix", yt_fwd_parse("r:2222:10.0.0.5:22", &f) && f.type == 1);
    check("parse without prefix defaults L", yt_fwd_parse("8080:localhost:80", &f) && f.type == 0);
    check("parse rejects bad port", yt_fwd_parse("L:70000:localhost:80", &f) == 0);
    check("parse rejects short", yt_fwd_parse("L:8080", &f) == 0);
    check("parse rejects empty", yt_fwd_parse("   ", &f) == 0);

    char buf[128];
    YtFwd g = { 0, "8080", "localhost", "80" };
    yt_fwd_format(&g, buf, sizeof(buf));
    check("format L", strcmp(buf, "L:8080:localhost:80") == 0);
    YtFwd d = { 2, "1080", "", "" };
    yt_fwd_format(&d, buf, sizeof(buf));
    check("format D", strcmp(buf, "D:1080") == 0);
    check("roundtrip", yt_fwd_parse(buf, &f) && yt_fwd_format(&f, buf, sizeof(buf)) && strcmp(buf, "D:1080") == 0);

    /* 高亮规则 */
    YtRule rules[8];
    int n = yt_rules_parse("ERROR=>#FF5555;WARN=>#FFAA00\n# 注释行\nFAILED=>#ff0000", rules, 8);
    check("rules count", n == 3);
    check("rules first text", strcmp(rules[0].text, "ERROR") == 0);
    check("rules color red", (rules[0].color & 0xFFFFFF) == 0xFF5555);
    check("rules skip comment", n == 3 && strcmp(rules[1].text, "WARN") == 0);
    check("rules default color when missing", yt_rules_parse("hi", rules, 8) == 0);
    int m = yt_rules_parse("ok=>#00FF00", rules, 1);
    check("rules max clamp", m == 1 && rules[0].color != 0);

    /* 标签截断 */
    wchar_t out[64];
    yt_tab_shorten(L"short", 20, out, 64);
    check("shorten keeps short", wcscmp(out, L"short") == 0);
    yt_tab_shorten(L"abcdefghijklmnop", 8, out, 64);
    check("shorten adds ellipsis", wcslen(out) == 8 && out[7] == 0x2026);
    yt_tab_shorten(L"中文标签名称很长", 7, out, 64);
    int cells = 0;
    for (size_t i = 0; i < wcslen(out); i++) cells += (out[i] >= 0x2E80 && out[i] < 0xFF00) ? 2 : 1;
    check("shorten wide char cells", cells <= 7);
    yt_tab_shorten(NULL, 10, out, 64);
    check("shorten null safe", out[0] == 0);

    printf("\nResult: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
