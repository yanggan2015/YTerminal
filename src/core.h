/* YTerminal - core.h : 无 UI 依赖的纯逻辑（可单元测试） */
#ifndef YT_CORE_H
#define YT_CORE_H

#include "util.h"

/* 端口转发规格：L 本地 / R 远端 / D 动态(SOCKS) */
typedef struct {
    int  type;          /* 0=L 1=R 2=D */
    char listen[96];    /* 监听：[bind:]port */
    char host[128];     /* 目标主机（D 类型为空） */
    char dest[32];      /* 目标端口（D 类型为空） */
} YtFwd;

/* 高亮规则：文本 -> 颜色（0x01RRGGBB） */
typedef struct {
    char text[64];
    u32  color;
} YtRule;

/* 模糊匹配：needle 是否按子序列出现在 hay 中；返回 0=不匹配，>0 分数（越大越靠前） */
int yt_fuzzy_score(const char *needle, const char *hay);

/* 解析一行转发规格，如 "L:8080:localhost:80" / "R:9000:db:5432" / "D:1080"
   返回 1 成功、0 失败 */
int yt_fwd_parse(const char *line, YtFwd *out);
/* 格式化为规范字符串，返回写出的长度 */
int yt_fwd_format(const YtFwd *f, char *buf, int cch);

/* 解析高亮规则，源串用 ; 或换行分隔，每条形如 "ERROR=>#FF5555"
   返回条目数 */
int yt_rules_parse(const char *src, YtRule *out, int max);

/* 按终端单元格宽度截断（宽字符占 2 格，超长补省略号） */
void yt_tab_shorten(const wchar_t *in, int max_cells, wchar_t *out, int cch);


/* 输入历史（每个终端一份，纯逻辑，便于单测） */
#define YT_HIST_MAX 40
#define YT_HIST_LEN 240
typedef struct {
    char items[YT_HIST_MAX][YT_HIST_LEN];
    int  n;
    int  nav;      /* -1 = 正在编辑当前行；否则为历史下标 */
} YtHist;

void        yt_hist_init(YtHist *h);
int         yt_hist_add(YtHist *h, const char *line);   /* 1=已加入，0=空行/与上一条重复 */
const char *yt_hist_prev(YtHist *h);                    /* NULL=没有更早的 */
const char *yt_hist_next(YtHist *h);                    /* 返回 "" 表示回到当前空行，NULL=未在浏览 */
int         yt_hist_unique(const YtHist *h, const char *prefix, char *out, int cap);
int         yt_utf8_cells(const char *s);               /* UTF-8 码点数（近似显示格数） */
#endif
