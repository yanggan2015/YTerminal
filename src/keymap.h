/* YTerminal - keymap.h : 可自定义快捷键（解析/格式化/冲突检测/持久化） */
#ifndef YT_KEYMAP_H
#define YT_KEYMAP_H

#include <windows.h>

#define YT_KM_CTRL  1
#define YT_KM_SHIFT 2
#define YT_KM_ALT   4
#define YT_KM_MAX   32

typedef struct {
    int action;            /* 命令 ID */
    const wchar_t *name;   /* 显示名（中文） */
    const char *def;       /* 默认键，如 "Ctrl+Shift+C" */
} YtKeyDef;

void            yt_key_init(void);
int             yt_key_count(void);
const YtKeyDef *yt_key_def(int i);
const char     *yt_key_spec(int i);                 /* 当前键（"" 表示已解绑） */
int             yt_key_cmd(int i);
void            yt_key_display(int i, wchar_t *out, int cap); /* "Ctrl+Shift+C" 或 "未绑定" */

int  yt_key_parse(const char *spec, int *mods, int *vk);      /* 0=成功 */
void yt_key_format(int mods, int vk, char *out, int cap);

int  yt_key_bind(int i, const char *spec, int *conflict);     /* 0=成功；-1=无法解析；-2=与他人冲突 */
void yt_key_unbind(int i);
void yt_key_reset(int i);
void yt_key_reset_all(void);

int  yt_key_lookup(int ctrl, int shift, int alt, int vk);     /* 命中返回命令 ID，否则 0 */
int  yt_key_conflicts(void);                                  /* 当前冲突条数 */

const char *yt_key_dump(void);                                /* "0=Ctrl+Shift+C;1=..."，静态缓冲 */
int         yt_key_load(const char *blob);                    /* 逐项覆盖，非法项跳过 */

#endif
