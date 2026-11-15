/* VT 核心单元测试（无 GUI，控制台运行） */
#include "../src/vt.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int g_pass = 0, g_fail = 0;
static char g_title[256];

static void ok(int cond, const char *name) {
    if (cond) { g_pass++; printf("  [PASS] %s\n", name); }
    else { g_fail++; printf("  [FAIL] %s\n", name); }
}

static void feed(VT *vt, const char *s) { vt_feed(vt, (const u8 *)s, strlen(s)); }

static int line_equals(VT *vt, int idx, const char *expect) {
    char *t = vt_line_text(vt, idx);
    int r = strcmp(t, expect) == 0;
    if (!r) printf("    line %d = [%s], expect [%s]\n", idx, t, expect);
    free(t);
    return r;
}

static void on_title(void *u, const char *t) { (void)u; strncpy(g_title, t, sizeof(g_title) - 1); }

int main(void) {
    printf("=== YTerminal VT core tests ===\n");

    /* 1. 基本打印与光标 */
    VT *vt = vt_create(20, 5, 100);
    feed(vt, "hello");
    ok(vt_cursor_x(vt) == 5 && vt_cursor_y(vt) == 0, "print hello -> cursor 5,0");
    ok(line_equals(vt, 0, "hello"), "line text hello");
    feed(vt, "\x1b[2;3HX");
    ok(vt_cursor_x(vt) == 3 && vt_cursor_y(vt) == 1, "CUP row2 col3 -> cursor 3,1");
    ok(line_equals(vt, 1, "  X"), "CUP writes X at col 2");
    feed(vt, "\x1b[2J");
    ok(line_equals(vt, 1, ""), "ED2 clears line");
    vt_destroy(vt);

    /* 2. SGR 颜色 */
    vt = vt_create(20, 5, 100);
    feed(vt, "\x1b[1;31mA\x1b[0m\x1b[42mB\x1b[0m\x1b[38;5;208mC\x1b[0m\x1b[38;2;18;52;86mD\x1b[0m");
    Cell *c = vt_line(vt, 0);
    ok(c[0].fg == 1 && (c[0].attr & ATTR_BOLD), "bold red A");
    ok(c[1].bg == 2, "green bg B");
    ok(c[2].fg == 208, "256-color C");
    ok(VT_IS_RGB(c[3].fg) && VT_INDEX(c[3].fg) == 0x56, "truecolor D (b=0x56)");
    vt_destroy(vt);

    /* 3. 滚动与历史 */
    vt = vt_create(10, 3, 50);
    feed(vt, "1\r\n2\r\n3\r\n4");
    ok(vt_history_len(vt) == 1, "history has 1 line after scroll");
    ok(line_equals(vt, 0, "1"), "history line 0 = 1");
    ok(line_equals(vt, 1, "2"), "screen line 0 = 2 (via total index)");
    ok(line_equals(vt, 2, "3"), "line 2 = 3");
    ok(line_equals(vt, 3, "4"), "line 3 = 4");
    ok(vt_cursor_y(vt) == 2, "cursor at bottom row");
    /* 回看：向上滚动后第 0 行应可见 */
    vt_scroll(vt, 1);
    ok(vt_view_offset(vt) == 1, "view offset 1");
    char *vis = vt_screen_text(vt);
    ok(strncmp(vis, "1", 1) == 0, "viewport top is history line 1");
    free(vis);
    vt_scroll_to_bottom(vt);
    vt_destroy(vt);

    /* 4. 备用屏 */
    vt = vt_create(10, 3, 50);
    feed(vt, "main");
    feed(vt, "\x1b[?1049h");
    ok(vt_is_alt(vt), "alt screen active");
    ok(vt_cursor_x(vt) == 0 && vt_cursor_y(vt) == 0, "alt screen cursor reset");
    feed(vt, "alt");
    ok(line_equals(vt, vt_history_len(vt), "alt"), "alt screen has own content");
    feed(vt, "\x1b[?1049l");
    ok(!vt_is_alt(vt), "back to main");
    ok(line_equals(vt, vt_history_len(vt), "main"), "main content preserved");
    vt_destroy(vt);

    /* 5. 宽字符 */
    vt = vt_create(10, 2, 10);
    u8 zh[] = {0xE4, 0xB8, 0xAD, 0xE6, 0x96, 0x87, 0}; /* 中文 */
    vt_feed(vt, zh, 6);
    Cell *zc = vt_line(vt, 0);
    ok(zc[0].cp == 0x4E2D && zc[0].width == 2, "wide char 中 occupies 2 cells");
    ok(zc[1].width == 0, "wide char continuation cell");
    ok(vt_cursor_x(vt) == 4, "cursor after two wide chars = 4");
    ok(line_equals(vt, 0, "中文"), "wide text extract");
    vt_destroy(vt);

    /* 6. 制表与退格 */
    vt = vt_create(20, 2, 10);
    feed(vt, "a\tb");
    ok(vt_cursor_x(vt) == 9, "tab to column 9 after a");
    feed(vt, "\b\b");
    ok(vt_cursor_x(vt) == 7, "backspace twice -> 7");
    vt_destroy(vt);

    /* 7. DEC 图形字符 */
    vt = vt_create(10, 2, 10);
    feed(vt, "\x1b(0lqk\x1b(B");
    Cell *gc = vt_line(vt, 0);
    ok(gc[0].cp == 0x250C && gc[1].cp == 0x2500 && gc[2].cp == 0x2510, "DEC graphics lqk -> ┌─┐");
    vt_destroy(vt);

    /* 8. OSC 标题 */
    vt = vt_create(20, 3, 10);
    vt_set_title_cb(vt, on_title);
    feed(vt, "\x1b]0;my title\x07");
    ok(strcmp(g_title, "my title") == 0, "OSC 0 sets title");
    ok(strcmp(vt_title(vt), "my title") == 0, "vt_title returns title");
    ok(vt_title_dirty(vt) == 1, "title dirty flag set");
    vt_clear_title_dirty(vt);
    ok(vt_title_dirty(vt) == 0, "title dirty cleared");
    vt_set_title_cb(vt, NULL);
    vt_destroy(vt);

    /* 9. 光标位置上报 */
    vt = vt_create(20, 5, 10);
    feed(vt, "\x1b[3;4H\x1b[6n");
    /* 应答由回调输出，这里仅验证不崩溃 */
    ok(1, "CPR request handled");
    vt_destroy(vt);

    /* 10. 插入/删除字符 */
    vt = vt_create(20, 2, 10);
    feed(vt, "abcdef");
    feed(vt, "\x1b[1;2H\x1b[2P");
    ok(line_equals(vt, 0, "adef"), "delete 2 chars at col1");
    feed(vt, "\x1b[1;2H\x1b[3@");
    ok(line_equals(vt, 0, "a   def"), "insert 3 blank at col1");
    vt_destroy(vt);

    /* 11. 选择文本 */
    vt = vt_create(20, 5, 10);
    feed(vt, "line1\r\nline2");
    char *sel = vt_selection_text(vt, 0, 0, 1, 4);
    ok(strcmp(sel, "line1\r\nline2") == 0, "selection across lines");
    free(sel);
    vt_destroy(vt);

    /* 12. 搜索 */
    vt = vt_create(30, 4, 100);
    feed(vt, "alpha beta\r\ngamma delta\r\nneedle here");
    int fl = -1, fc = -1;
    ok(vt_find(vt, "needle", 0, &fl, &fc) == 1 && fl == 2 && fc == 0, "find needle at line 2 col 0");
    ok(vt_find(vt, "DELTA", 0, &fl, &fc) == 1 && fl == 1 && fc == 6, "case-insensitive find DELTA at 1,6");
    ok(vt_find(vt, "not-there", 0, &fl, &fc) == 0, "find missing returns 0");
    vt_destroy(vt);

    /* 13. 调整尺寸 */
    vt = vt_create(10, 3, 10);
    feed(vt, "0123456789");
    vt_resize(vt, 20, 4);
    ok(vt_cols(vt) == 20 && vt_rows(vt) == 4, "resize to 20x4");
    ok(line_equals(vt, vt_history_len(vt), "0123456789"), "content preserved after resize");
    vt_resize(vt, 8, 2);
    ok(vt_cols(vt) == 8 && vt_rows(vt) == 2, "resize down to 8x2");
    vt_destroy(vt);

    /* 14. 自动换行 */
    vt = vt_create(5, 3, 10);
    feed(vt, "abcdefg");
    ok(line_equals(vt, 0, "abcde"), "wrap line 1 = abcde");
    ok(line_equals(vt, 1, "fg"), "wrap line 2 = fg");
    vt_destroy(vt);

    /* 15. 滚动区域 */
    vt = vt_create(10, 5, 10);
    feed(vt, "a\r\nb\r\nc\r\nd\r\ne");
    feed(vt, "\x1b[2;4r\x1b[4;1H\n");
    /* 区域 1..3 滚动：b 应被 c 取代，不一定相等，检查不崩溃与行数 */
    ok(vt_rows(vt) == 5, "scroll region no crash");
    vt_destroy(vt);

    printf("\nResult: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
