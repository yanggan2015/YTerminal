/* YTerminal - util.h : 基础工具（UTF-8/宽字符/字符串/路径/时间/动态数组） */
#ifndef YT_UTIL_H
#define YT_UTIL_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>

#ifndef YT_VERSION
#define YT_VERSION L"1.0.0"
#endif
#define YT_APP_NAME L"YTerminal"

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned short u16;

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
wchar_t *xwcsdup(const wchar_t *s);

/* ---- 字符串构建器 ---- */
typedef struct {
    char *buf;
    size_t len, cap;
} StrBuf;

void sb_init(StrBuf *sb);
void sb_free(StrBuf *sb);
void sb_append(StrBuf *sb, const char *s);
void sb_append_n(StrBuf *sb, const char *s, size_t n);
void sb_appendf(StrBuf *sb, const char *fmt, ...);
void sb_appendw(StrBuf *sb, const wchar_t *s);
void sb_clear(StrBuf *sb);
char *sb_detach(StrBuf *sb);

/* ---- 宽字符/UTF-8 ---- */
wchar_t *utf8_to_wide(const char *s);
char *wide_to_utf8(const wchar_t *s);
int utf8_decode_one(const unsigned char *s, size_t len, u32 *cp); /* 返回消耗字节，0 表示无效 */
size_t utf8_encode_one(u32 cp, char *out);                       /* 返回写入字节 */
int ucs_width(u32 cp);                                           /* 0/1/2 列宽 */

/* ---- 路径 ---- */
void yt_app_dir(wchar_t *out, int cch);      /* %APPDATA%\YTerminal */
void yt_exe_dir(wchar_t *out, int cch);
BOOL yt_mkdir(const wchar_t *path);
void yt_ensure_dirs(void);
BOOL yt_file_exists(const wchar_t *path);
BOOL yt_write_file(const wchar_t *path, const void *data, size_t len);
char *yt_read_file(const wchar_t *path, size_t *out_len);

/* ---- 时间 ---- */
void yt_now_local_str(wchar_t *out, int cch, const wchar_t *fmt);
void yt_now_time_str(wchar_t *out, int cch);   /* HH:MM:SS */
void yt_now_date_str(wchar_t *out, int cch);   /* YYYY-MM-DD */
void yt_now_stamp(wchar_t *out, int cch);      /* YYYYMMDD_HHMMSS */

/* ---- 会话编码 ---- */
enum { YT_ENC_UTF8 = 0, YT_ENC_GBK = 1, YT_ENC_BIG5 = 2, YT_ENC_SJIS = 3, YT_ENC_EUCKR = 4 };
int   yt_enc_codepage(int enc);
const wchar_t *yt_enc_name(int enc);
char *yt_bytes_to_utf8(const char *data, size_t n, int enc, size_t *out_len);
char *yt_utf8_to_bytes(const char *data, size_t n, int enc, size_t *out_len);
int   yt_enc_is_lead(int enc, unsigned char b);   /* 双字节编码的首字节 */

/* ---- 其他 ---- */
int yt_clampi(int v, int lo, int hi);
void yt_trim(char *s);
BOOL yt_contains_ci(const char *hay, const char *needle);
void yt_basename(const wchar_t *path, wchar_t *out, int cch);

/* 简易通配符匹配（* 与 ?，忽略大小写） */
BOOL yt_wildcard_match(const char *pattern, const char *text);

#endif
