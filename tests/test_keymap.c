/* YTerminal - tests/test_keymap.c : 快捷键表解析 / 格式化 / 冲突 / 存取 */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "../src/ids.h"
#include "../src/keymap.h"

static int g_pass = 0, g_fail = 0;

static void ok(int cond, const char *what) {
    if (cond) { g_pass++; }
    else { g_fail++; printf("FAIL  %s\n", what); }
}

int main(void) {
    int m = 0, vk = 0;

    yt_key_init();

    ok(yt_key_count() >= 16, "快捷键条目不少于 16 项");

    /* 解析 */
    ok(yt_key_parse("Ctrl+Shift+P", &m, &vk) == 0 && m == (YT_KM_CTRL | YT_KM_SHIFT) && vk == 'P',
       "解析 Ctrl+Shift+P");
    ok(yt_key_parse("ctrl+shift+c", &m, &vk) == 0 && m == 3 && vk == 'C', "解析大小写不敏感");
    ok(yt_key_parse("Ctrl+=", &m, &vk) == 0 && m == YT_KM_CTRL && vk == VK_OEM_PLUS, "解析 Ctrl+=");
    ok(yt_key_parse("Ctrl+-", &m, &vk) == 0 && vk == VK_OEM_MINUS, "解析 Ctrl+-");
    ok(yt_key_parse("F11", &m, &vk) == 0 && m == 0 && vk == VK_F11, "解析 F11");
    ok(yt_key_parse("Ctrl+Alt+Tab", &m, &vk) == 0 && m == (YT_KM_CTRL | YT_KM_ALT) && vk == VK_TAB,
       "解析 Ctrl+Alt+Tab");
    ok(yt_key_parse("Ctrl+Shift+Nope", &m, &vk) != 0, "非法键名被拒");
    ok(yt_key_parse("", &m, &vk) != 0, "空串被拒");
    ok(yt_key_parse("Ctrl+Shift+", &m, &vk) != 0, "缺主键被拒");

    /* 格式化往返 */
    {
        char out[64];
        yt_key_format(YT_KM_CTRL | YT_KM_SHIFT, 'P', out, sizeof(out));
        ok(strcmp(out, "Ctrl+Shift+P") == 0, "格式化 Ctrl+Shift+P");
        ok(yt_key_parse(out, &m, &vk) == 0 && m == 3 && vk == 'P', "格式化后可再解析");
        yt_key_format(YT_KM_CTRL, VK_OEM_PLUS, out, sizeof(out));
        ok(strcmp(out, "Ctrl+=") == 0, "格式化 Ctrl+=");
        yt_key_format(0, VK_F5, out, sizeof(out));
        ok(strcmp(out, "F5") == 0, "格式化 F5");
    }

    /* 默认表命中 */
    ok(yt_key_lookup(1, 1, 0, 'C') == IDC_COPY, "默认 Ctrl+Shift+C = 复制");
    ok(yt_key_lookup(1, 0, 0, 'W') == IDC_CLOSE_TAB, "默认 Ctrl+W = 关闭标签");
    ok(yt_key_lookup(1, 0, 0, VK_TAB) == IDC_NEW_TAB, "默认 Ctrl+Tab = 新建标签");
    ok(yt_key_lookup(0, 1, 0, VK_TAB) == 0, "Shift+Tab 不占用");
    ok(yt_key_lookup(1, 1, 0, 'X') == 0, "未绑定组合返回 0");

    /* 冲突检测 */
    {
        int conflict = -1;
        int rc = yt_key_bind(0, "Ctrl+Shift+V", &conflict);  /* 与粘贴冲突 */
        ok(rc == -2 && conflict == 1, "冲突被拦截并指出对手");
        ok(strcmp(yt_key_spec(0), "Ctrl+Shift+C") == 0, "冲突时原键不变");
    }

    /* 改键生效 + 旧键失效 */
    {
        int conflict = -1;
        ok(yt_key_bind(0, "Ctrl+Alt+J", &conflict) == 0, "改键成功");
        ok(yt_key_lookup(1, 0, 1, 'J') == IDC_COPY, "新键命中");
        ok(yt_key_lookup(1, 1, 0, 'C') == 0, "旧键已失效");
    }

    /* 解绑 */
    yt_key_unbind(0);
    ok(strcmp(yt_key_spec(0), "") == 0, "解绑后 spec 为空");
    ok(yt_key_lookup(1, 0, 1, 'J') == 0, "解绑后不命中");

    /* 序列化 / 反序列化 */
    {
        const char *dump = yt_key_dump();
        ok(strstr(dump, "0=") != NULL, "dump 含索引 0");
        yt_key_reset_all();
        ok(yt_key_lookup(1, 0, 1, 'J') == 0, "恢复默认后自定义失效");
        ok(yt_key_lookup(1, 1, 0, 'C') == IDC_COPY, "恢复默认后 Ctrl+Shift+C 生效");
    }

    /* 加载时非法项跳过，不破坏默认 */
    {
        yt_key_load("0=Ctrl+Alt+K;99=Ctrl+X;3=BadKey");
        ok(yt_key_lookup(1, 0, 1, 'K') == IDC_COPY, "加载自定义键生效");
        ok(yt_key_lookup(1, 1, 0, 'A') == IDC_SELECTALL, "非法项不影响其他默认");
    }

    /* 冲突统计 */
    ok(yt_key_conflicts() == 0, "默认表无冲突");

    printf("\nResult: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
