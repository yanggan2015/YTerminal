/* YTerminal - proto.h : XMODEM / YMODEM / ZMODEM 收发（无 UI 依赖） */
#ifndef YT_PROTO_H
#define YT_PROTO_H

#include "util.h"

/* 抽象字节流：真实串口/SSH 与单测的回环都实现这三个回调 */
typedef struct {
    void *ctx;
    /* 写出一段字节，返回实际写出数（<0 表示致命错误） */
    int (*put)(void *ctx, const u8 *buf, int len);
    /* 阻塞读，最多 timeout_ms 毫秒，返回读到的字节数（0=超时，<0=错误） */
    int (*get)(void *ctx, u8 *buf, int len, int timeout_ms);
    /* 进度回调，可为 NULL；done/total 以字节计，total<0 表示未知 */
    void (*progress)(void *ctx, const char *name, long done, long total);
} YtProtoIO;

/* 返回 0 成功；<0 失败（-1 IO/-2 超时/-3 校验失败/-4 对端取消/-5 参数） */
int yt_xmodem_send(YtProtoIO *io, const char *path, int use_crc);
int yt_xmodem_recv(YtProtoIO *io, const char *save_path, int use_crc);
int yt_ymodem_send(YtProtoIO *io, const char *path);
int yt_ymodem_recv(YtProtoIO *io, const char *dir);
int yt_zmodem_send(YtProtoIO *io, const char *path);
int yt_zmodem_recv(YtProtoIO *io, const char *dir);

const char *yt_proto_error(int code);

#endif
