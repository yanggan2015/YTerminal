#ifndef YT_KEY_CHARS_H
#define YT_KEY_CHARS_H
/* 键序列 DSL：普通文本 + <ctrl+c> <enter> <esc> #delay:100 */
int yt_keys_expand(const char *spec, char *out, int cap);
#endif
