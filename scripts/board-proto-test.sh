#!/bin/sh
# YTerminal 协议互通自测脚本（在开发板 / Linux 设备上运行）
#
# 用途：为 XMODEM / YMODEM / ZMODEM 提供“对端”，配合 YTerminal 发送/接收文件。
# 用法：把本脚本拷到板子上，chmod +x，然后执行：
#     ./board-proto-test.sh menu          # 交互菜单，逐项手动测
#     ./board-proto-test.sh send z        # 板子发文件，PC 端“接收”来验证
#     ./board-proto-test.sh recv x        # 板子收文件，PC 端“发送”来验证
#     ./board-proto-test.sh info          # 只打印环境与测试文件信息
#
# 依赖：lrzsz（提供 sz/rz/sx/rx/sb/rb）；BusyBox 环境至少要有 rx。
# 本脚本不联网、不安装软件，只做本地文件与串口侧动作。

set -u
DIR=/tmp/yterm-proto
FILE=$DIR/probe.bin
UP=$DIR/upload.bin

say() { printf '%s\n' "$*"; }

prepare() {
    mkdir -p "$DIR" 2>/dev/null || true
    if [ ! -s "$FILE" ]; then
        # 生成 512KB 可重复内容（不用 urandom，便于两端比对）
        i=0
        : > "$FILE"
        while [ $i -lt 512 ]; do
            printf 'YTerminal proto test block %04d 0123456789abcdef\n' "$i" >> "$FILE"
            i=$((i + 1))
        done
        # 追加 64KB 二进制噪声，覆盖转义/边界情况
        if [ -r /dev/urandom ]; then
            dd if=/dev/urandom bs=1024 count=64 >> "$FILE" 2>/dev/null || true
        fi
    fi
    rm -f "$UP" 2>/dev/null || true
}

have() { command -v "$1" >/dev/null 2>&1; }

info() {
    say "== YTerminal proto peer =="
    say "kernel : $(uname -srm 2>/dev/null)"
    say "shell  : $0"
    say "file   : $FILE $( [ -f "$FILE" ] && wc -c < "$FILE" || echo missing ) bytes"
    for t in sz rz sx rx sb rb; do
        if have "$t"; then say "tool   : $t -> $(command -v $t)"; fi
    done
    if have busybox; then
        say "busybox: $(busybox --list 2>/dev/null | grep -E '^(rx|sx|sz|rz)$' | tr '\n' ' ')"
    fi
    if have md5sum; then say "md5    : $(md5sum "$FILE" 2>/dev/null)"; fi
    if have sha256sum; then say "sha256 : $(sha256sum "$FILE" 2>/dev/null)"; fi
}

send_one() {
    # $1 = x|y|z
    prepare
    case "$1" in
        x) if have sx; then say "sx  $FILE  （PC 端选 XMODEM 接收）"; exec sx "$FILE"; else say "缺少 sx（lrzsz）"; fi ;;
        y) if have sb; then say "sb  $FILE  （PC 端选 YMODEM 接收）"; exec sb "$FILE"; else say "缺少 sb（lrzsz）"; fi ;;
        z) if have sz; then say "sz  $FILE  （PC 端选 ZMODEM 接收）"; exec sz "$FILE"; else say "缺少 sz（lrzsz）"; fi ;;
        *) say "未知协议：$1（可选 x/y/z）" ;;
    esac
}

recv_one() {
    # $1 = x|y|z
    prepare
    case "$1" in
        x) if have rx; then say "rx  $UP  （PC 端选 XMODEM 发送）"; exec rx "$UP"; else say "缺少 rx"; fi ;;
        y) if have rb; then say "rb  $DIR  （PC 端选 YMODEM 发送）"; exec rb "$DIR"; else say "缺少 rb（lrzsz）"; fi ;;
        z) if have rz; then say "rz -y  $DIR  （PC 端选 ZMODEM 发送）"; exec rz -y "$DIR"; else say "缺少 rz（lrzsz）"; fi ;;
        *) say "未知协议：$1（可选 x/y/z）" ;;
    esac
}

verify() {
    prepare
    say "接收结果："
    ls -l "$UP" 2>/dev/null || say "  还没有收到文件（$UP）"
    if have md5sum; then md5sum "$UP" 2>/dev/null; fi
}

menu() {
    prepare
    while :; do
        say ""
        say "== YTerminal 协议自测菜单 =="
        say " 1) 板子发文件 XMODEM (sx)   -> PC 接收"
        say " 2) 板子发文件 YMODEM (sb)   -> PC 接收"
        say " 3) 板子发文件 ZMODEM (sz)   -> PC 接收"
        say " 4) 板子收文件 XMODEM (rx)   -> PC 发送"
        say " 5) 板子收文件 YMODEM (rb)   -> PC 发送"
        say " 6) 板子收文件 ZMODEM (rz)   -> PC 发送"
        say " 7) 显示测试文件与环境信息"
        say " 8) 校验收到的文件"
        say " 0) 退出"
        printf '选择: '
        read -r c || break
        case "$c" in
            1) send_one x ;;
            2) send_one y ;;
            3) send_one z ;;
            4) recv_one x ;;
            5) recv_one y ;;
            6) recv_one z ;;
            7) info ;;
            8) verify ;;
            0) break ;;
            *) say "无效选择" ;;
        esac
    done
}

case "${1:-menu}" in
    menu) menu ;;
    send) send_one "${2:-z}" ;;
    recv) recv_one "${2:-z}" ;;
    info) info ;;
    verify) verify ;;
    *) say "用法: $0 menu | send x|y|z | recv x|y|z | info | verify" ;;
esac
