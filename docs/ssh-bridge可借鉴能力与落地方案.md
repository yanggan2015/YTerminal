# YTerminal 可借鉴 ssh-bridge 的能力清单与落地方案

> 编写日期：2026-11-15
> 对比对象：
> - 本项目：`C:\code\YTerminal`（v1.0.0，Win32 原生终端，绿色包 `dist/YTerminal`）
> - 参照项目：`C:\code\ssh-bridge`（v20261113-153629，板卡通道服务 + Agent HTTP 接口 + WebUI）
>
> 本文只做分析与方案，**未修改任何一方的源码**。所有"实现方案"均以新增文件 + 局部改造点描述，供后续按迭代落地。
>
> **结构**：分两部分。
> - **第一部分（B1–B23）**：能力层——"它有什么功能，我要不要补、怎么补"；
> - **第二部分（D1–D17）**：设计层——"它用什么工程范式组织这件事"，多为几十行代码换一个数量级体验的项。
>
> 两部分独立可读，但 D 部分是 B 部分的实现纪律（尤其 D1/D2/D3 应先于 B4，D6/D7/D11 应先于 B15）。

---

## 0. 阅读范围与方法

### 0.1 遍历方式

两边都按"源码优先、文档仅作索引"的方式逐文件读：

| 工程 | 逐文件读过的源码 | 说明 |
|---|---|---|
| YTerminal | `src/ui/main.c`(1861 行)、`src/ui/term_view.c`(735)、`src/ui/ui_extra.c`(253)、`src/core/*.c`(session 90 / profile_store 184 / cred_store 137 / window_store 91 / features 132 / paths 29)、`src/engine/*.c`(ssh_session 318 / ssh_async 28 / local_pty 237 / serial_session 210 / sftp 148 / forward 155 / zmodem 56 / remotes 87)、`bridge/win_ui_bridge.c`(678)、`src/core/yt_core.h`(190)、`build.ps1`、`tools/pack.ps1`、`docs/*.md` | 6 个历史遗留文件（`src/main.c`、`src/pty_session.c`、`src/serial_session.c`、`src/session.c`、`src/term_view.c`、`src/dlg.c`）**不在 `build.ps1` 的编译列表内**，属于死代码 |
| ssh-bridge | `src/http_manager.c`(8418)、`src/ssh_shell_bridge.c`(928)、`src/serial_bridge.c`(1521)、`src/log_buffer.c`(682)、`src/auth.c`(1261)、`src/config.c`(400)、`src/util.c`(775)、`src/tftp_svc.c`(649)、`src/relay_ctrl.c`(202)、`src/channel_core.c`(30)、`src/lat_trace.c`(69)、`src/self_update.c`(683)、`src/discovery.c`(367)、`src/script_lua.c`(561)、`src/tcp_forward.c`(431)、`src/tunnel_mgr.c`(431)、`src/inbound_gateway.c`、`src/key_chars.c`(242)、`src/job_runner.c`(53)、`src/desktop_win.cpp`(299)、`windows_manager/main.cpp`(436)、`include/*.h`、`Makefile`、`webui_static/*.html`、`tests/*`、`docs/*.md`、`DESIGN.md`、`TOOL_STANDARD.md` | 重点看"通道层怎么在 Windows 上稳定跑"和"机器接口/工程纪律怎么设计"，不看 EADK 业务细节 |

### 0.2 关键前提：两个工程不是一个物种

| 维度 | YTerminal | ssh-bridge |
|---|---|---|
| 使用者 | **人**（工程师坐在电脑前操作终端） | **机器/Agent**（脚本、HTTP 客户端、另一个 Agent） |
| 形态 | Win32 原生 GUI，单 exe 绿色包 | 常驻服务（HTTP 8086 + WebUI），可打包成桌面壳 |
| 核心对象 | 会话标签（本地/SSH/串口/RDP/VNC/X11） | 通道（串口板卡 / SSH link），长期在线 |
| 数据主链路 | 通道 → `PostMessage(WM_YT_OUTPUT)` → 终端控件渲染 | 通道 → 环形缓冲 → 多客户端（WebUI/WS/入站 SSH）同时读 |
| 对外接口 | 一个仅测试用的 UI 桥（`/ui-tree`、`/state`、`/logs`、`/action`），Release 包**不含** | 67 个 operations 的 Agent API + 自描述 `/api/help` + 锁租约 |
| 主要风险 | 交互延迟、大文件传输卡 UI、串口高波特率丢字、断线要手点 | 公网暴露、多 Agent 抢占通道 |

**结论**：能借鉴的不是"业务功能"（TFTP/NFS/继电器对终端产品没意义），而是它在**通道层工程范式、机器接口纪律、可观测性、工程基线**上的成熟做法——而这几点恰好是 YTerminal 当前最薄的。

### 0.3 先说清现状：看板与代码不符的 8 处

`docs/合并功能列表.md` 把 YT-01～YT-35 全部标为 `[x]`，但按代码核对，下列 8 项是占位或半成品。方案设计必须以代码为准，否则会重复劳动或误判工期。

| 看板项 | 声明的能力 | 代码实际 | 证据 |
|---|---|---|---|
| YT-12 会话日志 | 时间戳日志 | 原始字节 `fwrite`，无时间戳、无分日、无轮转；文件名按**标签序号** `tab%d.log`（同一序号复用时串档） | `src/core/session.c:27-30`、`src/ui/main.c:1562-1564` |
| YT-14 编码显示 | UTF-8/编码 | 只按 UTF-8 解析，无 GBK/Latin1 切换 | `term_append` 只做 UTF-8 多字节解析 |
| YT-19 ZMODEM | rz/sz + B64 回退 | 不是协议实现：RX 只是发 `rz -y`；TX 是 `cat > file <<EOF` + base64 逐行 `Sleep(5)` 的伪传输 | `src/engine/zmodem.c:5-13`、`:15-58` |
| YT-23 布局保存 | layout.json | 空实现：`yt_layout_load` 恒返回 `ntabs=0`，只恢复 `split` | `src/core/window_store.c:78-97` |
| YT-26 断线重连 | 自动重连 | 只有菜单"重新连接"手动重建（复制会话再 start） | `src/ui/main.c:1168`、`:1399-1411` |
| YT-29 脚本自动化 | 逐行脚本 | 逐行写 + `Sleep(80)`，无等待/无读取/无分支，且**在 UI 线程**执行 | `src/core/features.c:100-117` |
| YT-04 串口校验/流控 | 校验/流控 | 结构体有 `data_bits/stop_bits/parity/flow`，但属性框只暴露 COM + 波特率 | `src/ui/ui_extra.c:164-178`、`:217-218` |
| YT-33 GPU 渲染 | （已标 `~`，不计） | GDI 基线，可接受 | — |

另有一处**必须处理的安全问题**：`src/core/profile_store.c:174-193` 的首次启动默认书签里硬编码了真实主机地址与 root 明文口令，会随安装包一起分发。本文最后一节给出处理建议（不在文档里复述这些口令）。

> 补充说明（避免误判责任）：从 git 工作区可以看出，本项目经历过一次**精简重写**——`src/vt.c`、`src/vt.h`、`src/ui_sftp.c`、`src/keymap.c`、`tests/test_vt.c` 等在 v1.4.0 后退出了工作区，现在的 `src/` 是更小的实现。因此上表不是"虚报"，而是**看板未随重写更新**。这也意味着：被砍掉的 VT 能力（完整 VT 解析、键位映射）与本文要补的通道层能力是两件不同的事，不要混在一次改造里。

---

## 1. 可借鉴能力总览（23 项）

优先级：**P0** = 直接影响正确性/体验，建议本轮做；**P1** = 差异化竞争力；**P2** = 产品化/可选。

| 编号 | 能力 | ssh-bridge 出处 | YTerminal 现状 | 优先级 | 预估 |
|---|---|---|---|---|---|
| B1 | 通道能力抽象（vtable 统一串口/SSH/本地） | `include/bridge.h:21-63` | 只有 `write_fn/resize_fn/close_fn` 三个指针 | P0 | 2 人日 |
| B2 | 输出环形缓冲 + 单调游标（增量读/尾部回放） | `include/log_buffer.h`、`src/log_buffer.c`(733) | 文本只存在控件里，外部读不到、切换标签要重建 | P0 | 2.5 人日 |
| B3 | 日志治理（时间戳/去ANSI/CR 归一/分日/落盘线程） | `log_buffer.c:63-87`、`:690-732` | 原始字节直写单文件 | P0 | 1.5 人日 |
| B4 | 串口 Overlapped + `WaitCommEvent` 事件驱动 IO | `serial_bridge.c:424-436`、`:574-695` | 同步 `ReadFile` + 20ms 超时 + `Sleep(1)`，受 15.6ms 定时器量化限制 | P0 | 2 人日 |
| B5 | 自动重连 / 看门狗 / `want_open` 语义 | `serial_bridge.c:34`、`:612-695`；`ssh_shell_bridge.c:338-350` | 读失败即关闭，只能手点重连；SSH 无 keepalive | P0 | 2 人日 |
| B6 | 读写分离 + 发送队列（写不阻塞调用方） | `ssh_shell_bridge.c:352-419`、`serial_bridge.c:151-171` | 写入在 UI 线程且持会话锁；SSH 写是 EAGAIN 忙等 | P0 | 2.5 人日 |
| B7 | 行尾/编码/控制线（DTR/RTS/BREAK）在线切换 | `include/config.h`（`line_ending/encoding/dtr/rts`）、`/serial/lines` | 无行尾转换、无编码、无 DTR/RTS/BREAK | P1 | 2 人日 |
| B8 | 键序列 DSL（`<ctrl+c>` `<esc>` `<up>` `#delay`） | `src/key_chars.c:22-115` | 只能"发送字符串"，控制键无法脚本化 | P1 | 1 人日 |
| B9 | 终端态识别（uboot/login-password/shell） | `http_manager.c`（`detect_state`，约 2497-2576） | 只有 CONNECTED/CLOSED | P1 | 1.5 人日 |
| B10 | 标记阻塞执行（`cmd; echo __END` 等 marker 取区间输出） | `http_manager.c:2717-2820` | 无"命令-输出"绑定 | P1 | 1.5 人日 |
| B11 | 真实 XMODEM-CRC / YMODEM 传输 | `http_manager.c:7159-7260` | ZMODEM 是伪实现，U-Boot 场景不可用 | P1 | 3 人日 |
| B12 | 后台任务框架 + 进度（不堵 UI） | `src/job_runner.c`(53) | SFTP/脚本全在 UI 线程同步跑 | P1 | 2 人日 |
| B13 | 分片上传 + offset 续传 + md5 校验 + 结构化目录 | `http_manager.c:1178-1322` | 一次流式到底，无进度/校验/续传 | P1 | 3 人日 |
| B14 | 配置补丁式持久化 + 版本迁移 + 热应用 | `config.c:296-344`、`/channel/configure`、`/reload` | 手写 `strchr` 解析、整表重写、改配置需重连 | P1 | 2 人日 |
| B15 | Agent 接口规范（`/help` `/health` `/lock` `/exec` `/logs` 批处理 限流 审计） | `http_manager.c:4250-4422`、`:6113-6122`、`auth.c` | 单 in-flight、无自描述、无锁、无数据面 | P1 | 4 人日 |
| B16 | Lua 脚本（沙箱 + 通道 API + deadline） | `src/script_lua.c` | 逐行 + `Sleep(80)` | P2 | 4 人日 |
| B17 | 自更新（独立 updater + 白名单替换 + 回滚） | `self_update.c`、`updater_main.c` | 只有 `tools/pack.ps1` 手工打包 | P2 | 3 人日 |
| B18 | 局域网发现（UDP 广播 + 清单） | `src/discovery.c`、`windows_manager/main.cpp` | 无 | P2 | 2 人日 |
| B19 | 共享会话（入站接入，多人看同一串口） | `inbound_gateway.c:27-232`、`ssh_shell_bridge.c:498-607` | 只有"同步输入"（本机多标签） | P2 | 5 人日 |
| B20 | 内置 Web 控制台 / WebView2 壳 | `webui_static/*`、`tools/gen_webui_embed.py`、`desktop_win.cpp` | 无（有 mongoose 可作后端） | P2 | 4 人日 |
| B21 | 隧道托管（外置 frp/ngrok + 健康探测） | `tunnel_mgr.h`、`tunnel_mgr.c` | 无 | P2 | 2 人日 |
| B22 | 转发增强（远端 `-R`、端口段、多客户端并发） | `tcp_forward.h/.c` | 仅本地 `L`，且 accept 循环一次只服务一个连接 | P1 | 2 人日 |
| B23 | 工程基线（严格告警/单测/质量门/规范/清死代码） | `Makefile`、`tests/*`、`docs/C-CODING-STANDARD.md` | 只有 `-Wall`、零自动化测试 | P0 | 2 人日 |

> 合计约 **55 人日**，其中 P0 部分（B1-B6、B23 基线）约 **15 人日** 即可把"通道层正确性"补齐。
>
> 另有**第二部分（D1–D17）**：设计范式层，约 **36 人日**，其中 4.5 人日可在一天内落地并立即见效（见文末第二部分）。

---

## 2. 逐项落地方案

> 每项固定四段：**借鉴什么** → **本项目现状** → **怎么实现**（文件/结构/流程） → **验收**。
> 代码片段是设计示意，不是可直接提交的补丁。

### B1 通道能力抽象（把串口/SSH/本地收敛成一层）

**借鉴**：ssh-bridge 用一个 `Bridge` 结构 + 函数指针做"通道能力表"，所有功能（HTTP 命令、WebUI 流、脚本、入站 SSH、日志导出）都只跟这张表打交道，因此新增一种通道只需要实现表项，上层全部复用。表里有 `start/stop/running/status_json/enqueue_command/write_raw/send_command_block/send_keys/reconnect/control/clear_logs/get_log_*/set_log_disk/destroy`（`include/bridge.h:21-63`）。

**现状**：`YtSession` 只有 `write_fn / resize_fn / close_fn` 三个指针（`src/core/yt_core.h:60-78`），没有"读"这一侧、没有状态 JSON、没有控制面。于是：日志在 `session.c` 里做、重连在各引擎里各写一遍、桥接只能读 UI 树拿不到终端字节、脚本只能"盲发"。`serial_session.c`、`ssh_session.c`、`local_pty.c` 三者结构几乎不共享。

**怎么实现**

1. 新增 `src/core/channel.h` / `channel.c`，定义本项目的能力表（字段按需，不照抄全量）：

```c
typedef struct YtChannel YtChannel;
typedef enum { YT_CH_LOCAL, YT_CH_SSH, YT_CH_SERIAL } YtChType;

struct YtChannel {
    YtChType type;
    char     id[64];              /* 稳定 id：kind://user@host:port 或 COM3@baud */
    char     name[64];
    void    *impl;
    YtRing  *ring;                /* 见 B2 */
    int      want_open;           /* 用户意图：我要它开着（B5 用） */
    int      auto_reconnect;
    int      state;               /* YT_CH_CLOSED/OPENING/OPEN/RETRY_WAIT */
    char     last_error[160];

    int  (*open)(YtChannel *);
    void (*stop)(YtChannel *);
    int  (*is_open)(YtChannel *);
    int  (*write_raw)(YtChannel *, const void *p, int n);   /* 只入队，不阻塞 */
    int  (*send_keys)(YtChannel *, const char *spec, int dur_ms, int interval_ms);
    int  (*control)(YtChannel *, const char *action, const char *json, char **out);
    int  (*status_json)(YtChannel *, char *out, int cap);
    void (*destroy)(YtChannel *);
};
```

2. 迁移策略（**先串口，后 SSH，最后本地**，每步可独立发版）：
   - 第 1 步：`serial_session.c` 内部改成实现 `YtChannel`，`YtSession` 增加 `YtChannel *ch` 字段；`yt_session_write()` 变成 `return s->ch->write_raw(...)` 的薄封装，原有调用点（`main.c` 里 30+ 处）不改。
   - 第 2 步：`ssh_session.c` 同样接入，顺带把读侧改为 publish 到 `ring`（B6）。
   - 第 3 步：`local_pty.c`（ConPTY）接入。
3. 兼容：保留 `write_fn/resize_fn/close_fn` 一个版本周期，`channel.c` 里做适配，避免一次性大改 UI。

**验收**：`grep -r "PostMessageW(hwnd_term, WM_YT_OUTPUT"` 只剩 `channel.c` 一处；三个引擎不再各自持有日志/重连逻辑；新增一个"Telnet 会话"只需一个 ~200 行文件即可在 UI 出现。

---

### B2 输出环形缓冲 + 单调游标（终端"可被读取"）

**借鉴**：ssh-bridge 的 `LogBuffer` 是通道与所有消费者之间唯一的真相来源。它的接口设计非常关键（`include/log_buffer.h`）：

- `log_buffer_get_since(epoch, seen, &n)`：**单调游标**增量读。调用方保存上次 `(epoch, seen)`，下次只拿新字节；缓冲回绕时 `epoch+1`，调用方据此丢弃游标重新全量。
- `get_log_notify_seq()`：无锁通知计数器，WS/UI 空闲时用"序号没变就跳过"，避免定时轮询。
- `get_tail_chars(n)` / `get_tail_lines(n)`：切标签/首屏只取尾部，不做全量重放。
- `log_buffer_dup`：一次性快照（入站 SSH 做 exec 时用）。

**现状**：终端文本只存在于 `term_view` 的网格缓冲（初始 `YT_COLS 120`，`WM_SIZE` 时按窗口重算列数并回调 `yt_session_resize`；滚动回看 `YT_SCROLLBACK 10000` 行，`term_view.c:614-641`），外部（桥接、脚本、导出）拿不到原始字节；`term_append` 是逐字节解析。切换/重开标签时历史丢失；桥接的 `/logs` 只能读调试日志，不是会话输出。

**怎么实现**

新增 `src/core/yt_ring.h` / `yt_ring.c`（约 400 行）：

```c
typedef struct {
    CRITICAL_SECTION lock;
    unsigned char *buf; size_t cap, len, start, seq;
    uint64_t epoch, seen;              /* 游标：回绕时 epoch++ */
    volatile uint64_t notify_seq;      /* 生产者递增，消费者跳过空转 */
    double ts_first, ts_last;
    int  timestamp, strip_ansi, cr_norm;
    int  ansi_state;                   /* 0=正文 1=CSI 2=OSC */
    int  disk_on, disk_dirty;
    char disk_path[YT_PATH_MAX];
    FILE *disk;
} YtRing;

void     yt_ring_init(YtRing *, size_t cap);
void     yt_ring_set_cap(YtRing *, size_t cap);          /* 滚动扩缩容，保留尾部 */
void     yt_ring_append(YtRing *, const void *p, size_t n);
unsigned char *yt_ring_since(YtRing *, uint64_t *epoch, uint64_t *seen, size_t *n);
char    *yt_ring_tail_chars(YtRing *, size_t n);
char    *yt_ring_tail_lines(YtRing *, size_t lines);
void     yt_ring_clear(YtRing *);
void     yt_ring_set_timestamp(YtRing *, int on);         /* 运行时可切 */
void     yt_ring_set_disk(YtRing *, const wchar_t *dir, const char *name, int on);
```

实现要点（照搬它验证过的机制，不照搬代码）：

1. `append` 里一次性完成三件事：写入环形区、`seen += n`、`notify_seq++`。**不要在 append 里做 UI 调用**。
2. `since` 做边界判定：`*seen` 超出 `[seen-len, seen]` 区间、或 `epoch` 不匹配 → 返回 `NULL` + 调用方按"重新全量"处理。
3. 尾部读取用 `tail_chars`，UI 切换标签时一次 `SetWindowText` 级回放（本项目 `term_append` 支持批量，可一次喂 64KB），避免上千次 `AppendText`。
4. 容量建议：串口/SSH 默认 2 MiB（可在属性页 256 KiB～16 MiB 调），落盘另算；本项目 `YT_SCROLLBACK` 的行数只影响渲染，不影响 ring。
5. 与渲染的关系：`channel.c` 收到字节 → `yt_ring_append()` → `PostMessage(WM_YT_OUTPUT)` 只带"有新数据"信号，`term_append` 仍在 UI 线程从 ring 取尾部一段渲染。**这样终端渲染失败/重开也能从 ring 重建**。

**验收**（单测可机器判）：
- 写入 300 万字节后回绕 3 次，`since` 得到的拼接结果与尾部 `tail_chars` 完全一致；
- `epoch` 变化时调用方被正确通知；
- 10 个并发生产者 × 1 万次 append 无数据错位（CRC 对比）。

---

### B3 日志治理：时间戳 / 去 ANSI / CR 归一 / 分日 / 落盘线程

**借鉴**：ssh-bridge 在写缓冲时顺手把日志变成"人能看、机器也能解析"的形态（`log_buffer.c:63-72`）：

- 时间戳**只在行首、且不在 ANSI 序列中间**插入 `[HH:MM:SS.mmm] `（用 `ansi_state` 判断），否则会把时间戳塞进 ESC 序列里毁掉渲染；
- 处理 `\r`：`\r` 后紧跟 `\n` 合并为一次换行；`\r` + `\b` 视为擦除；`\r` 单独出现（进度条刷新）时，把该行标记为"行内覆盖"，在"文本导出/去 ANSI"视图里按最终内容重写；
- 落盘文件名 `name_YYYYMMDD.log`（`format_log_path`，`log_buffer.c:75-87`），按天分文件；
- 写盘由一个 flusher 线程（200ms / 64KB 触发）负责，`setvbuf(_IOFBF, 65536)`，**调用方永不 `fwrite`**；
- `set_log_disk()` 支持运行中开/关，并保证重新开启时把 flusher 线程拉回来（`log_buffer.c:690-732`）。

**现状**：`session.c:27-30` 直接 `fwrite` 原始字节，注释里也写了"avoid per-chunk timestamp + fflush (major lag)"——说明当初是为了不卡而牺牲了日志质量。文件名 `tab%d.log`（`main.c:1562`）按标签序号，多会话/重开标签会混档；无分日、无去 ANSI、无轮转、无大小上限。

**怎么实现**

1. 日志能力收敛到 `YtRing`（B2 的 `disk_*` 字段），`session.c` 里删除 `log_fp` 相关逻辑，改为 `yt_ring_set_disk()`。
2. 命名改为"会话 id + 日期"：`logs/<kind>_<user>@<host>_<YYYYMMDD>.log`，id 由 `channel.c` 生成（B1 的 `id` 字段），彻底解决串档。
3. 三个视图分开：
   - **raw**（落盘默认）：原始字节，保真，供回放/取证；
   - **pretty**（导出/看板默认）：按上面的规则加时间戳、去 ANSI、CR 归一；
   - **plain**（复制粘贴用）：只去 ANSI + CR 归一，不加时间戳。
4. 属性页加三选项：`时间戳`、`去 ANSI（导出时）`、`保留原始（调试）`；`log_on` 独立于 ring 是否开（ring 永远开，落盘可选）。
5. 轮转：单文件超 64 MiB 或跨天即换新文件；保留 30 天（可配）。

**验收**：跑一个会打印进度条的命令（`\r` 刷新）+ `ls --color=always`（大量 ESC），pretty 视图无乱码、时间戳在行首、无 `ESC[...` 残留；raw 文件字节数与串口收字节数一致（用 `certutil -hashfile` 对比）。

---

### B4 串口：Overlapped + `WaitCommEvent` 事件驱动

**借鉴**：ssh-bridge 的串口读循环刻意避开了 Windows 定时器量化陷阱（`serial_bridge.c:424-436`、`:574-695`）：

- `CreateFile(..., FILE_FLAG_OVERLAPPED)`；
- `SetCommMask(h, EV_RXCHAR|EV_ERR|EV_CTS|EV_DSR|EV_RLSD|EV_BREAK)`；
- reader 线程：`WaitCommEvent`（异步）→ `WaitForSingleObject(hEvent)` → `ClearCommError` 取 `cbInQue` → 按 `cbInQue` 批量读；
- 完全不依赖 `COMMTIMEOUTS` 短超时，也不 `Sleep(1)` 空转；停止用 `CancelIoEx` + `SetCommMask(h,0)` + 关句柄。
- 代码注释里直接写了"Windows 定时器 15.6ms 量化"这个坑，说明是实测踩出来的。

**现状**：`serial_session.c` 用同步 `ReadFile` + `COMMTIMEOUTS{ReadIntervalTimeout=1, ReadTotalTimeoutConstant=20}` + `Sleep(1)`，并在一次读里按 16ms/4096B 攒批。后果：
- 交互延迟地板 ≈ 15～30ms（终端打字有"粘手感"）；
- 921600 / 1500000 波特率下 20ms 可积压 20～30KB，单次 4KB 读会溢出驱动 FIFO，**丢字节**；
- reader 线程空转 `Sleep(1)`，CPU 有常驻开销。

**怎么实现**（重写 `serial_session.c` 的 reader，其余 API 不动）

```c
static DWORD WINAPI serial_reader(LPVOID arg) {
    YtChannel *ch = arg; SerialImpl *im = ch->impl;
    OVERLAPPED ov = {0}; ov.hEvent = im->rx_ev;
    while (im->run) {
        DWORD mask = 0;
        if (!WaitCommEvent(im->h, &mask, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) break;
        }
        DWORD w = WaitForSingleObject(ov.hEvent, 200);   /* 只为让 stop 有机会退出 */
        if (w == WAIT_TIMEOUT) { continue; }
        ResetEvent(ov.hEvent);
        COMSTAT st = {0}; DWORD err = 0;
        ClearCommError(im->h, &err, &st);                 /* 顺便拿 CE_RXOVER 计数 */
        while (st.cbInQue) {
            DWORD ask = min(st.cbInQue, 8192);
            if (!ReadFileOverlapped(im->h, im->buf, ask, &got, &im->rd_ov)) break;
            if (got) { yt_ring_append(ch->ring, im->buf, got); im->rx += got; }
            ClearCommError(im->h, &err, &st);
            st.cbInQue -= min(st.cbInQue, got);
        }
        if (err & CE_RXOVER) im->overrun++;
    }
    return 0;
}
```

配套必须做的三件小事：
1. `SetupComm(h, 64*1024, 16*1024)` 提高驱动队列；
2. `SetCommMask` 里带上 `EV_ERR`，把 `CE_RXOVER/CE_FRAME/CE_OVERRUN` 计数进 `status_json`（这就是 ssh-bridge 状态里能看到错误累计的价值）；
3. 删除所有 `Sleep(1)`；`stop` 里 `CancelIoEx(h, NULL)` 后等 reader 退出再 `CloseHandle`（否则会出现半关闭句柄）。

**验收**：用它的思路自建压测——目标机 `cat` 一个 20MB 文件从串口出来（921600），比对字节数；`serial_session` 的 `overrun` 计数必须为 0；交互往返延迟用 `tests` 里的 `probe_lat` 思路（发 `\r` 打时间戳）从 ~25ms 降到 <3ms。

---

### B5 自动重连 / 看门狗 / `want_open`

**借鉴**：ssh-bridge 把"用户意图"和"物理状态"分开记：`want_open`（我要它开）+ `running`（现在开没开）+ `last_error` + 重连计数 + 启动时间。串口 reader 出错时不是直接死，而是关句柄 → 记错误 → 睡 `SERIAL_RECONNECT_DELAY_MS` → 若 `want_open` 仍为真则重开（`serial_bridge.c:612-695`）。SSH 侧另有一个 supervisor 线程每 1s 检查链路，写命令前 `wait_shell_ready`（`ssh_shell_bridge.c:338-350`）。

**现状**：串口 reader 读失败 → `break` → `PostMessage(WM_YT_CLOSED)` → 状态 CLOSED；SSH 没有 `libssh2_keepalive_config`，链路假死时只能等 TCP 超时；UI 只有菜单"重新连接"（`main.c:1168`，做法是复制会话对象再 start，会把 SFTP/转发状态丢掉）。

**怎么实现**

1. 一个**全局通道监管线程**（不要每通道一个线程）：`yt_channel_supervisor`，200ms tick，遍历所有 `open` 过的通道，状态机：

```
CLOSED --(open)--> OPENING --ok--> OPEN --(err)--> RETRY_WAIT --(退避到点)--> OPENING
                     ^                                   |
                     +-------- want_open=0 (用户点了断开) -+
```

2. 退避：1s → 2s → 5s → 10s → 30s 封顶；连续稳定 60s 归零；`reconnect_tries` 进 `status_json`，UI 状态栏显示"重连中 (第 3 次) · 上次错误: 设备被占用"。
3. SSH 心跳：`libssh2_keepalive_config(sess, 1, 15)`，每 5s `libssh2_keepalive_send()`；连续 3 次失败即判死并重连（这是终端产品最需要的"Wi-Fi 抖动不掉线"）。
4. 重连后必须**保留**：编码/行尾/日志开关/转发规则——所以状态要放在 `YtChannel` 而不是 `impl`（这也是 B1 的价值）。
5. UI 增加"会话断开后自动重连"全局开关（默认开，可关）；手动"断开"要把 `want_open=0`，否则会被监管线程立刻重连（这是最容易出的逻辑 bug）。
6. 本地会话（ConPTY）不参与重连，`YT_CH_LOCAL` 直接跳过。

**验收**：串口运行中拔 USB → 5s 内自动恢复；SSH 会话用 `netsh` 断开网卡 10s → 恢复后 `echo` 正常且日志连续；手动"断开"后不再自动连（看 `status_json` 的 `want_open=0`）。

---

### B6 读写分离 + 发送队列

**借鉴**：ssh-bridge 每个通道都是"读线程 + 写线程"，`write_raw` **只入队**并返回，调用方（HTTP 线程、UI、脚本）永不阻塞；写线程负责把队列按 `interval` 分片吐出（`send_keys` 的延时就是这么实现的），并在写之前判断"链路是否就绪"（`ssh_shell_bridge.c:352-419`、`serial_bridge.c:151-171`）。

**现状**：
- `yt_session_write()` 在调用线程里直接执行 `write_fn`，且**持 `s->lock`**（`session.c:35-41`）；
- SSH 的 `write_fn` 是 `while(left>0){ w=libssh2_channel_write(...); if(w==EAGAIN) continue; }` 的忙等（`ssh_session.c`），慢链路时长按一次键就把 UI 线程拉满；
- 同步输入 `yt_sync_broadcast` 在 UI 线程里串行写 N 个会话（`session.c:79-88`），8 个标签同时输入会明显卡（因为每个都持锁 + 可能忙等）。

**怎么实现**

1. 新增 `src/core/byteq.h/.c`（定长环形字节队列，256KB 上限）+ 每通道一个 `writer` 线程：

```c
int yt_channel_write(YtChannel *ch, const void *p, int n) {
    if (!ch || ch->state != YT_CH_OPEN) return -1;
    if (byteq_push(&ch->txq, p, n) != 0) { ch->tx_dropped += n; return -2; }
    SetEvent(ch->tx_ev);
    return 0;
}
```

2. 写线程：`WaitForSingleObject(tx_ev, 5)` → 取一批（≤8KB）→ 分片写（`send_keys` 用 interval）→ 失败则整批回退并入 `last_error`；`stop` 时 `tx_ev` + `run=0` 让它退出。
3. 优先级：现场按键 > 脚本/宏 > 同步输入广播。给队列加两个"口"（高/低），写线程先清高的。
4. 溢出策略：用户按键永不丢（队列满时给 UI 一个"发送积压"提示）；脚本/广播可丢并计数。
5. 与锁的关系：`s->lock` 只保护指针/状态字段，**不跨越 IO**。这是 ssh-bridge 让 UI 线程始终轻的核心。

**验收**：限速 10KB/s 的 SSH 隧道下，长按方向键 5s 窗口不卡（任务管理器 UI 线程占用 <5%）；8 会话同步输入整包入队 <10ms；`tx_dropped` 在有丢包时有计数且 UI 有提示。

---

### B7 行尾 / 编码 / 控制线（DTR/RTS/BREAK）

**借鉴**：ssh-bridge 把串口会话参数做全了（`include/config.h` 的 `BoardConfig`）：`line_ending`（lf/cr/crlf）、`encoding`（utf-8/gbk/latin1）、`dtr`/`rts`、`auto_open`、`timestamp`、`buffer_size`、`log_dir`；运行期还能 `/serial/lines` 改 DTR/RTS、`control("break")` 发 BREAK、`/serial/configure` 在线改波特率并 `persist:true` 落盘。

**现状**：`YtSession` 有 `data_bits/stop_bits/parity/flow`（`yt_core.h:91-95`），但属性框只渲染 COM + 波特率（`ui_extra.c:164-178`），这三个字段等于死字段；无行尾转换（发什么就是什么）、无编码（只 UTF-8）、无 DTR/RTS/BREAK 按钮、无"打开串口时拉 DTR"这类板卡调试必需能力。

**怎么实现**

1. 数据模型加 4 个字段到 `YtSession`（并在 `profile_store` 持久化）：
   `int line_ending`(0 raw/1 LF/2 CR/3 CRLF)、`int encoding`(0 UTF-8/1 GBK/2 Latin1)、`int dtr`/`int rts`（0 不控/1 置位/2 清除）。
2. 转换只放在两个"门"上，别散在 UI：
   - **发送门**（`writer` 线程内）：按 `line_ending` 把 `\n` 改写；按 `encoding` 把 UTF-8 解成目标编码（GBK 用 `MultiByteToWideChar(CP_ACP 950/936)+WideCharToMultiByte`）；
   - **接收门**（reader 线程内）：按 `encoding` 把字节转成 UTF-8 再进 ring（**ring 内永远是 UTF-8**，下游全部简化）。
3. 控制线：`EscapeCommFunction(h, SETDTR/CLRDTR/SETRTS/CLRRTS)`；BREAK 用 `EscapeCommFunction(h, SETBREAK)` + 200ms 后 `CLRBREAK`（不要只 `SetCommBreak` 忘了恢复）。
4. UI：串口标签页底部加一条工具条 `DTR○ RTS○ BREAK [发送 Ctrl-C]`；属性框补 6 个控件（数据位/停止位/校验/流控/行尾/编码）。
5. 热应用：这些参数都可在连接中生效（不需要重开），只有 `COM 口/波特率` 需要重开——UI 明确提示（借 ssh-bridge 的 `need_reopen` 语义）。

**验收**：目标机跑 GBK 输出的程序，界面中文正常；`cat -v` 验证行尾三态；逻辑分析仪看 DTR/RTS 电平随按钮变化；U-Boot 里 `loadx` 前用 BREAK 复位能进下载模式。

---

### B8 键序列 DSL（`<ctrl+c>` / `<esc>` / `#delay`）

**借鉴**：`src/key_chars.c:22-115` 的 `parse_key_chars()`：支持 `<ctrl+c>`、`<esc>`、`<tab>`、`<enter>`、`<bs>`、`<up/down/left/right>`、`<f1..f12>`、`^C` 写法、`\r\n\t`、`\xHH`、`#delay:200`（段间延时），输出"字节数组 + 分段长度数组"，供 `send_keys` 按段间延时发送并统计已发送条数。

**现状**：只有"发送字符串"（`IDM_SEND_STR`），无法表达控制键；想发 `Ctrl-C` 只能靠宏录制真人按键；脚本/桥接/看板都没有统一入口。

**怎么实现**

1. 新增 `src/core/key_seq.h/.c`（约 200 行 + 单测）：

```c
typedef struct { unsigned char *b; int n; int *piece; int npieces; } YtKeySeq;
int yt_key_parse(const char *spec, YtKeySeq *out);   /* 0 ok，负值=第几个字符非法 */
void yt_key_free(YtKeySeq *);
```

2. 词法表与它保持一致（这类表很稳定，直接对齐用户习惯）：`<ctrl+字母>` → `0x01..0x1A`（`ctrl+[`=0x1B, `ctrl+\`=0x1C, `ctrl+]`=0x1D）、`<esc>`=0x1B、`<tab>`=0x09、`<enter>`=0x0D、`<bs>`=0x08、方向键 `ESC[A/B/C/D`、功能键 `ESC[11~..`、`^X` 写法、`\xHH`、`#delay:N`。
3. 三个入口共用同一个解析器：工具栏"发送键序列"输入框、脚本 `yt.keys()`、Agent 接口 `/keys`。
4. 内置三条（终端产品高频）：`<enter>`、`<ctrl+c>`、`<ctrl+]>`（telnet 逃生）；再给串口用户加 `<break>`（映射到 BREAK 控制线，而不是字节）。

**验收**：按其 `tests/test_key_chars.c` 的用例表改造一份（含非法输入、超长、`#delay` 叠加），断言 100% 通过；界面上用"发送 `<ctrl+c>`"中断目标机 `sleep 100`。

---

### B9 终端态识别（U-Boot / 登录页 / Shell）

**借鉴**：ssh-bridge 的 `/agent/probe` 从日志尾部（约 6 万字符）倒序匹配特征串，给出 `state ∈ {disconnected, login, password, uboot, shell, unknown}`（`http_manager.c` 约 2497-2576）。这让"自动登录、自动进 U-Boot、命令前判断状态"变成可能。

**现状**：状态只有 `YT_ST_CONNECTED/CLOSED/ERROR`，UI 状态栏没有"对端现在在什么界面"的概念；串口调试时用户靠眼睛判断 `=>` 还是 `login:`。

**怎么实现**

1. 新增 `src/core/state_probe.h/.c`：输入"尾部 N 行文本 + 最近输出时间"，输出状态 + 置信度 + 命中行。
2. 规则外置到 `config/state_rules.json`（可热加载）：

```json
{
  "uboot":   { "any": ["U-Boot", "=>", "Hit any key"], "window_lines": 8 },
  "login":   { "any": ["login:", "Username:"],          "window_lines": 5 },
  "password":{ "any": ["Password:", "password:"],       "window_lines": 5 },
  "shell":   { "any": ["# ", "$ ", "> "],               "window_lines": 3 }
}
```

3. 判定条件要"双保险"，避免历史残留误判：**命中 + 最近 2s 内有新输出**；稳定 N 次 tick 才切换（防抖）。
4. UI：状态栏加一段 `[U-Boot]`；右键状态出三个动作：`进 U-Boot`（连发 `<enter>`×3 + `<ctrl+c>`）、`登录`（用会话里的用户名/口令，借 B8 发送）、`回 Shell`（发 `<enter>`）。
5. 与 Agent 接口联动：`/channels/{id}/probe` 返回同结构，便于外部脚本"等板子起来再发命令"。

**验收**：真板串口从上电到 Shell 全程，状态变化顺序为 `unknown → uboot → login → shell`，误判次数 0；规则文件改 `window_lines` 后无需重启生效。

---

### B10 标记阻塞执行（命令-输出绑定）

**借鉴**：ssh-bridge 的 `/agent/exec` 不靠"睡够时间"，而是把命令包成 `cmd; echo end_cmd_<hex>`，然后轮询日志等 marker 出现，取 `echo marker` 与 marker 之间的文本作为输出，超时返回 `timed_out=true` + 已收到的部分输出；整个过程跑在 job 线程，不堵 HTTP（`http_manager.c:2717-2820`，marker 生成在 `:8130`）。

**现状**：脚本逐行 `Sleep(80)`（`features.c:100-117`），慢命令（`apt install`、`flash_erase`）必然错位；也没有"取某条命令的输出"这一能力，导致自动登录/自动采集只能靠人看。

**怎么实现**

1. 新增 `int yt_channel_exec(YtChannel *ch, const char *cmd, int timeout_ms, char **out, int *out_len, int *timed_out)`：
   - 生成 marker `__YT_END_<8hex>`（随机，避免与回显冲突）；
   - 发送 `cmd + "\r" + "echo " + marker + "\r"`；
   - 用 `yt_ring` 的 `(epoch, seen)` 游标从发送点开始扫，先跳过命令回显里第一次出现的 marker，再等第二处；
   - 取区间文本（去 ANSI、去行尾 `\r`），超时则返回已有内容并置 `timed_out`。
2. 调用方：脚本 `yt.exec()`、宏里的"等待提示符"、Agent `/exec`、以及 B9 的"进 U-Boot"（发 `version` 并取输出判断）。
3. 边界：目标机没有 shell（U-Boot 有 `echo`，但 BusyBox 精简版可能没有）——提供退化模式：`timeout_ms` 内"静默 300ms 视为结束"（可作为 `mode=quiet`）。
4. 输出上限（如 512KB）截断并标注 `truncated:true`（照它 `probe` 的 6 万字符上限思路）。

**验收**：`exec("uname -a")` 在 1s 内返回且内容不含 marker；`exec("sleep 5; echo hi", 2000)` 返回 `timed_out` 且 `hi` 未出现；连续 50 次调用无串话。

---

### B11 真实 XMODEM-CRC / YMODEM（替掉伪 ZMODEM）

**借鉴**：ssh-bridge 实现了完整 XMODEM-CRC 发送端（`http_manager.c:7159-7260`）：`SOH`(128B)/`STX`(1024B) 帧头、块号、CRC-16/XMODEM、`NAK` 重传（上限 10 次）、`EOT` 连发两次、双 `CAN` 取消、按 `ACK` 推进、超时按文件大小动态放宽，整段在 job 线程跑。

**现状**：`zmodem.c` 不是协议实现：RX 只写 `rz -y`；TX 若本地文件存在，就 `cat > file <<EOF` + 逐行 base64 + `Sleep(5)`，然后远端 `base64 -d`。对 **U-Boot**（只有 `loadx/loady/ymodem`，且串口是唯一通道）完全不可用，而且会"看起来成功"（`zmodem.c:5-58`）。

**怎么实现**

1. 新增 `src/engine/xmodem.c`（发送 + 接收，约 400 行）与 `src/engine/ymodem.c`（批量文件，后续）。
2. **不要走它"从日志文本里找 ACK 字节"的路**（那是它的脆弱点：协议字节混进日志、遇到 ANSI 或时间戳就崩）。正确做法是加一个 **tap 旁路**：
   - reader 收到字节 → `yt_ring_append()`（给人看）**同时** `if (ch->xfer) xmodem_feed(ch->xfer, buf, n)`（给协议看，原始字节）；
   - 传输期间对 UI 屏蔽协议字节（否则屏幕上全是乱码），只显示进度条。
3. 接口：`int yt_xfer_start(YtChannel *ch, int dir, const wchar_t *path)`，返回 job id；进度 `{sent, total, blocks, retries}`；取消按钮。
4. 同时纠正现有 ZMODEM 语义：菜单文案改成 `ZMODEM（需目标机 rz/sz）`，删掉 base64 假传输分支（现在静默成功是最危险的），并推荐 `XMODEM` 作为 U-Boot 默认。
5. 端口：先只做**串口**；SSH 场景仍用 SFTP（更可靠）。

**验收**：与 U-Boot `loadx` 对接传 8MB 镜像成功，块重传次数 ≤3；传 1KB 小文件正常；中途 Ctrl-C 能干净取消并回到 Shell；接收方向从 `sb`/U-Boot `savex` 拉文件成功。

---

### B12 后台任务框架 + 进度（把 UI 线程救回来）

**借鉴**：`src/job_runner.c`（仅 53 行）——单队列 + 工作线程 + `submit(name, work, done, ctx)`；重活（agent exec、HTTP 上传、xmodem、自更新）全丢进去，`done` 回到主线程再回响应。**关键在于"所有可能超过 50ms 的动作都不在 UI/HTTP 线程"这条纪律**。

**现状**：SFTP 列目录（`main.c:582-598`）、上传（`:1500-1511`）、下载、脚本执行（`features.c:100-117`）都在 UI 线程同步跑。更糟的是它们**在 `s->lock` 内**（`sftp.c` 整个传输持锁、`ssh_session.c` 的 reader 也要这把锁），所以上传 100MB 时不仅界面假死，**终端输出也停**——这是当前最影响观感的问题。

**怎么实现**

1. 新增 `src/core/job.h/.c`：2 个工作线程（`job_sftp`、`job_script` 分开，互不饿死），任务结构：

```c
typedef struct YtJob {
    int id; char name[64];
    void (*work)(struct YtJob *);
    void (*done)(struct YtJob *);
    void *ctx;
    volatile int progress, progress_max, cancel;
    char msg[160];
    int rc;
} YtJob;
int  yt_job_submit(const char *name, void (*work)(YtJob*), void (*done)(YtJob*), void *ctx);
void yt_job_cancel(int id);
int  yt_job_progress(int id, int *cur, int *max, char *msg, int cap);
```

2. UI 只做三件事：提交任务、`SetTimer` 200ms 刷进度、`WM_YT_JOB_DONE` 收结果；进度条放状态栏右侧（不要在模态框里堵人）。
3. **SFTP 句柄不能跨线程共享**。两种做法，推荐 (a)：
   - (a) 任务线程里独立开一条 SFTP 通道（`libssh2_sftp_init` 在该线程，用信号量保证同会话并发 ≤1）；传输不占 reader 的锁，终端输出不断流；
   - (b) 给会话加独立 `sftp_lock` 并把 SSH 会话设为阻塞模式——会拖慢 reader，不推荐。
4. 顺带把 `yt_ssh_libsession` 的调用约定写清楚（libssh2 会话对象非线程安全，必须"同一时刻只有一个线程动它"；用 `sftp_lock` + `reader` 用非阻塞轮询，或者干脆给 SFTP 单独建一条 SSH 连接——ssh-bridge 就是"每条通道连接独立"的思路）。

**验收**：上传 500MB 期间 UI 可切标签、终端持续有输出、进度条走动；点取消 1s 内停止且远端无残留 `.part`。

---

### B13 分片上传 / 续传 / 校验 / 结构化目录

**借鉴**：ssh-bridge 的 HTTP 上传支持 `?offset=`（分片）、`?md5=1`（末片才算，省时间）、返回 `size`/`md5`（`http_manager.c:1178-1235`、`:1274-1322`）；另有 `/tftp/md5` 单文件摘要用于"传完核对"。

**现状**：`yt_sftp_upload` 一次流式写到底；无进度、无校验、断线重来；`yt_sftp_listdir` 返回"换行分隔文本"，UI 用 ListBox + 字符串前缀 `D\t` 表示目录（`main.c:589-598`），无法排序、无法显示大小/时间。

**怎么实现**

1. `src/engine/sftp_ex.c`：
   - `yt_sftp_put(job, local, remote, chunk=64KB)`：分块写，每块后更新 `job->progress`；失败记录 `offset`；
   - `yt_sftp_resume(job, local, remote)`：远端 `stat` 取已有大小，从该 offset 追加（配合"本地大小一致"判断）；
   - 校验：默认算**本地** 的 SHA-256（CryptoAPI `CryptHashData`），远端若可执行则 `sha256sum` 比对，否则读回前/后 1MB 抽样比对（避免全量回读）。
2. 目录结构化：`typedef struct { char name[256]; uint64_t size; int is_dir; unsigned mode; time_t mtime; } YtSftpEntry;`，UI 换 `ListView`（列：名称/大小/时间/权限），支持排序、双击进目录、`..` 行。
3. 拖拽：`DragAcceptFiles` + `WM_DROPFILES` → 批量入队（每文件一个 job）。

**验收**：1GB 文件上传后 SHA-256 一致；中途拔网线，重连后"继续"从断点续传且最终校验通过；目录 5000 项时列表渲染 <200ms。

---

### B14 配置补丁式持久化 + 版本迁移 + 热应用

**借鉴**：ssh-bridge 改配置的姿势很值得抄（`config.c:296-344`）：

- 按 `name`（稳定标识）定位对象，**只写调用方给出的字段**，未给字段保持不变（避免"编辑一个会话把其它字段清空"这类事故）；
- `/boards` 增删只动目标并重算端口，**不重启其它通道**（surgical hot sync）；
- `/reload` 只同步 `ui_settings/discovery/auth`，单条配置坏了给 per-board `error` 而不整体失败；
- `/channel/configure` 在线改参数并 `persist:true` 落盘；
- 有 `config.json.example`、字段级 `sanitize/clamp`（端口范围、缓冲区上下限）。

**现状**：`profile_store.c` 用手写 `strchr`+`esc()` 拼 JSON（`92-146`），字段一多就容易错位；保存是**整表重写**；改一个会话的属性要重开会话才生效；无 `config.example`；`layout` 是空实现（见 0.3）。

**怎么实现**

1. 用仓库里已有的 `third_party/cJSON.c` 重写 `profile_store.c`：
   - `yt_profile_load()`：`cJSON_Parse` → 逐项映射；解析失败给"第 N 条损坏，已跳过"而不是整体崩；
   - `yt_profile_save()`：**临时文件 + `MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH)` 原子替换** + 先备份 `.bak`（ssh-bridge 这里是直接 `fopen("wb")`，不是原子写，我们要做得更好）；
   - `yt_profile_patch(const YtSession *one)`：按 `name`（未来改成稳定 `id`）定位后合并，只写该条。
2. 加 `"version": 2` 字段 + `migrate_v1_to_v2()`；未知字段保留（先 Parse 全量，只改已知 key，最后写回）。
3. 热应用分级：
   - `live`（无需重连）：标题/日志/时间戳/编码/行尾/DTR-RTS/转发规则/缓冲区大小；
   - `need_reopen`（需重连）：host/port/user/COM/baud/密钥；
   - UI 在保存时明确提示"以下字段将在下次连接生效"。
4. 补 `docs/config.example.json` 与 `config/state_rules.json`、`config/themes/*.json` 的 schema 注释（用 `//` 不行就写 `.md` 说明）。
5. 首次启动向导（3 步：选类型 → 填必需项 → 试连），替代现在"塞 5 条硬编码默认书签"的做法（同时解决安全隐患，见第 4 节）。

**验收**：手工把 `sessions.json` 的某条删掉一个字段、加一个未知字段 → 程序正常启动且未知字段在保存后仍在；改端口提示重连；保存过程中断电（`taskkill /f` 注入）后文件不损坏（有 `.bak`）。

---

### B15 Agent 接口 v2（把"测试桥"升级成"可被机器依赖的桥"）

**借鉴**：ssh-bridge 的接口纪律是它最有价值的部分，不只是功能：

1. **自描述**：`/api/help` 由"operations 表"驱动生成，包含 method/path/参数类型/示例，示例里的 board 名、端口等用**当前实例的真实数据**填充（`http_manager.c:4250-4422`，相当一部分是动态拼 curl 示例）。
2. **能力清单**：`/agent/scan` 返回 `channels[]`，每条带 `channel_type/port/state/reconnect/…`，Agent 先扫描再决定用什么（避免硬编码）。
3. **锁租约**：`POST /lock` + `X-Agent-Session` + `ttl_ms`、`/unlock` 同 session 才能释放（`http_manager.c:6113-6122`）。解决"两个 Agent 同时往串口发命令"的经典事故。
4. **健康优先**：`/health` 带 version/build/uptime，所有外部脚本与自更新都先打它。
5. **批量动作**：`/action` 一次多条 `{board,id}`，返回 `results[]`，单条失败不影响其它。
6. **安全分层**：token / 登录会话（HttpOnly + SameSite=Strict） / guest 只读 / 默认仅回环 / 公网暴露需显式开启 / 登录爆破锁定（5 次/10 分钟 → 锁 15 分钟） / `auth_audit.log` 审计（`auth.c:64-73`、`:103-119`、`:319-349`、`:571-634`）。

**现状**（`bridge/win_ui_bridge.c`，678 行）：只有 `/ping`、`/ui-tree`、`/state`、`/logs?since`、`/action`；token 可选；**全局只允许一个 in-flight action（第二个返回 429）**；没有 `/help`、没有版本、没有锁、没有终端数据面（读不到终端字节、发不了键）、没有批处理、没有审计；而且 Release 包不编译它。

**怎么实现**（新增 `src/agent/` 目录，只在 `-DYT_AGENT` 构建里编译，仍不进零售包）

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/health` | `{version, build, uptime_s, channels, ok}` |
| GET | `/api/help` | 表驱动生成 + 用本机真实通道名/端口拼示例 |
| GET | `/channels` | 通道清单（id/type/name/state/auto_reconnect/reconnect_tries/last_error/rx/tx） |
| GET | `/channels/{id}/log?since=&epoch=&tail_chars=&tail_lines=&strip=1` | 走 `yt_ring` 的增量与尾部；`strip=1` 去 ANSI |
| POST | `/channels/{id}/write` | 原始字节（base64 可选） |
| POST | `/channels/{id}/keys` | `{"spec":"<ctrl+c>#delay:100<enter>"}`（B8） |
| POST | `/channels/{id}/exec` | `{"cmd":"...","timeout_ms":5000}` → 标记阻塞（B10），job 化 |
| POST | `/channels/{id}/probe` | 终端态（B9） |
| POST | `/channels/{id}/control` | `open/close/break/set_lines/configure/resize`（B7） |
| POST | `/actions` | 批量 `[{channel, action, args}]` → `results[]` |
| POST/DELETE | `/lock` | `X-Agent-Session` + `ttl_ms` |
| GET | `/audit?tail=200` | 最近审计（时间/来源/通道/动作/结果） |

配套要求（缺一不可，否则会重复它的老问题）：
1. **默认只绑 127.0.0.1**；要对外必须在设置里显式打开并同时要求 token（不能只开不加锁）。
2. 令牌桶限流（如 200 req/s/通道），超限 429；错误响应统一 `{code,msg}`。
3. `Origin` 校验（浏览器跨站 POST 防护，照它的做法）；`SameSite=Strict` 若做 Web 登录。
4. `audit.log` 结构化追加：`ts | ip | session | channel | action | result | bytes`。
5. 所有动作返回 `job_id`（长任务），并提供 `GET /jobs/{id}` 查进度——这是"不堵 HTTP"的关键。
6. 版本化：对外路径同时给 `/v1/...`，旧路径保留一个版本周期。

**验收**：`/api/help` 里每条示例 curl 可直接复制执行成功（含真实通道名）；两个客户端同时 `/lock` 只有一个成功，过期自动释放；`/exec` 100 个并发不堵 `/health`（P95 <100ms）。

---

### B16 Lua 脚本（沙箱 + 通道 API + deadline）

**借鉴**：`src/script_lua.c` 把 Lua 5.4 vendor 进工程，每个"按钮脚本"是短脚本；环境**白名单**（不给全量 `io/os`），有 wall-clock deadline 检查，通道 API 是 `send / wait_mark / read_since / log_len / mark / msleep`；返回 `{output, error}`，超时明确报错。

**现状**：`features.c` 逐行 `Sleep(80)`，无读取、无分支、无失败处理，且在 UI 线程；宏录制只能回放固定时序。

**怎么实现**

1. vendor `third_party/lua`（约 2 万行，绿色包 +200KB 可接受；`build.ps1` 加 `$lsrc` 通配，`-x lua.c/luac.c/onelua.c/ltests.c`）。
2. 绑定面（保持小而稳，避免失控）：

```lua
yt.send("reboot\r")
yt.keys("<ctrl+c>#delay:300<enter>")
yt.sleep(200)
local ok, text = yt.expect("login:", 5000)     -- 等正则/子串，返回是否命中
local out = yt.exec("uname -a", 3000)          -- B10
local cur = yt.cursor(); local s = yt.log_since(cur)
local st = yt.state()                           -- B9
yt.progress(i, total)
```

3. 沙箱：只开 `base/string/table/math`，去掉 `io/os/package`；`debug` 关闭；每脚本 30s wall-clock（在 `lua_sethook` 里检查 `clock()`），可被 UI "停止"按钮打断。
4. 执行在 `job_script` 线程（B12），结果回 UI；失败给"第 N 行 + 错误"。
5. 兼容存量：把旧"逐行脚本文件"自动转成等价 Lua（`for line in ...: yt.send(line.."\r"); yt.sleep(80)`），老文件继续能用。

**验收**：脚本"等板子起来 → 发命令 → 抓输出 → 存文件"一条跑通；死循环脚本 30s 被杀且进程不挂；沙箱里 `io.open` 报 nil。

---

### B17 自更新（绿色包场景的轻量版）

**借鉴**：`self_update.c` + `updater_main.c` 的两段式：

1. 主进程下载 → 校验 → **启动独立 updater**（`--wait-pid <主进程>`）→ 自己退出；
2. updater 等主进程退出 → 解压到 staging → **按白名单替换**（保留 `config/`）→ 启动新版本；失败回退旧版本；
3. 前端轮询 `/system/update/status` 显示 `download → verify → stop → extract → swap → start` 各阶段；
4. 构建里打 `version_gen.h` 戳，保证 `/health` 与更新状态报同一个版本（它踩过"一个二进制里混两个版本号"的坑，`Makefile` 里专门加了依赖）。

**现状**：只有 `tools/pack.ps1` 手工打包，没有更新通道，也没有版本号戳（`YT_VERSION` 是硬编码字符串 `yt_core.h:22`）。

**怎么实现**

1. 新 target `YTerminal-update.exe`（约 250 行）：`--wait-pid P --zip Z --dir D`；`WaitForSingleObject` 等主进程 → `tar.exe -xf`（Win10 自带 bsdtar）或 zlib 解压 → 白名单 `YTerminal.exe / themes/ / runtime/ / *.dll` → 旧 exe 改名 `*.bak.<ver>` → 启新 exe → 起不来则回滚 `.bak`。
2. 主程序"检查更新"：拉 `latest.json`（`{version, url, sha256, notes}`）→ 下到 `config\update\` → **校验 sha256 通过才执行** → 拉起 updater → 退出。
3. 版本戳：`build.ps1` 生成 `src/core/version_gen.h`（`#define YT_BUILD "20261115-2130"`），所有目标依赖它（学它 `Makefile` 的那条规则）。
4. 保留 `config/ logs/ keys/`，并在启动时校验配置可解析，坏配置自动 `.bak` 并起向导。

**验收**：模拟替换到一半断电，重启后仍是可用版本（旧 exe 存在）；`config` 目录内容不变；`/health` 的 version 与更新记录一致。

---

### B18 / B19 局域网发现 + 会话共享（远程协助）

**借鉴（发现）**：`src/discovery.c` 用 UDP 广播应答：收到 magic（`SSHBM1`）就回 `{alias, port, boards[], version}`；`windows_manager/main.cpp` 就是拿它列"网内板卡"并内嵌对端 WebUI。要点是**应答体每次现读配置**（改了别名/端口不用重启）。

**借鉴（共享）**：`inbound_gateway.c:27-232` 起了个 libssh **服务端**（默认 2222），**用户名 = 通道名**、密码 = 该通道的入站口令（空则只宣告 `none` 免密）；认证通过后把 session 交给通道的 `inbound_fn`（`ssh_shell_bridge.c:498-607`），后者把远端键入写进同一通道、把 ring 增量回送 → **多人共享同一个串口，只读或可写**。

**现状**：只有"同步输入"（本机多标签同时发），没有"别人连进来看/发"。这对"远程协助同事排障"是刚需（YTerminal 用户群是嵌入式/运维）。

**怎么实现（分两阶段，成本差很大）**

- **阶段 1（低成本，推荐先做）**：用现有 mongoose 加 `WS /share/{channel}`（token 保护）：
  - 服务端把 `yt_ring` 的增量按 `since` 推给浏览器；浏览器用 xterm.js 渲染；
  - 键入按权限回写（`read-only` / `write` 两种，默认只读）；
  - UI 显示"N 人正在观看（其中 1 人可写）"，一键踢人/换权限；
  - 局域网内共享 `http://<本机IP>:8086/share/<id>?t=<一次性token>`。
- **阶段 2（可选）**：真 SSH 入站。**依赖代价要如实说明**：本项目只有 `libssh2`（客户端），要做 SSH 服务端需引入 `libssh`（+约 600KB，且两套 SSH 栈初始化/线程模型要共存）。只有在客户明确要求"用 ssh 连进来"时才做，且默认关闭、强口令、限定局域网。

**验收**：同事用浏览器打开链接能实时看到串口输出（延迟 <300ms），只读时输入被拒；断开后服务端不残留会话。

---

### B20 内置 Web 控制台 / WebView2 壳

**借鉴**：ssh-bridge 把前端全部塞进 exe：`webui_static/*.html/js/css` 由 `tools/gen_webui_embed.py` 生成 C 数组（`Makefile` 的 `embed` target），运行时直接从内存出页面，无需外部文件；`dashboard.html` 用 xterm.js + `/ws/log` 二进制帧 + `requestAnimationFrame` 合并渲染 + 切标签 `tail` 回放；`desktop_win.cpp` 用 WebView2 包一层壳，显示前先轮询 `/health`，并支持 `--smoke` 无人值守自检。

**现状**：纯 Win32，无 Web 视图。好处是轻，坏处是：手机/平板看不了、没法做"分享给同事"的界面、录屏回放也没有。

**怎么实现（可选）**

1. 后端复用 B15 的 Agent API + B18/B19 的发现与 WS；前端只做三页：看板（通道列表 + 实时日志）、发送（键序列/命令）、设置。
2. 资源嵌入：写 `tools/gen_assets.ps1` 把 `web/` 生成为 `src/agent/assets_gen.c`（字节数组 + `Content-Type` 表），`build.ps1` 里加一步；**不要**运行时读磁盘，否则绿色包会被质疑"夹带文件"。
3. 壳：可选加 WebView2（Win10/11 多数已预装运行时；缺失时降级为"用默认浏览器打开"），显示前 `/health` 探活，失败给重试按钮——这个"先探活再显示"的细节能省掉大量"白屏"投诉。
4. 明确边界：Web 端只做**只读 + 受限写**，桌面端能力不全部暴露。

**验收**：单 exe 启动后浏览器打开 `http://127.0.0.1:8086/` 能看到实时串口；`--smoke` 无头跑通返回 0。

---

### B21 隧道托管（把本机服务映射出去）

**借鉴**：`tunnel_mgr.h` 不自己实现穿透，而是**托管外部客户端进程**：把配置对象写成 runtime json → `CreateProcess` 起 `eadk-tunnel`（frp 系）→ `map_http` 映射本地 WebUI，可选第二个 frpc 映射入站 SSH → `health()` 打本地 HTTP 探活 → 提供 start/stop/running 与 HTTP/SSH 两套开关。

**现状**：无。要做"远程支持客户设备"就得靠用户自己搞内网穿透。

**怎么实现**

1. 抽象成 `src/net/tunnel.h`：`start(kind, local_port, remote, token)`，只支持"外置 exe"模式（`runtime\tunnel\*.exe`），生成临时配置到 `config\tunnel\<profile>.json`，日志重定向到 `logs\tunnel.log`。
2. 只暴露最小面：`/health`、`/share/*`；**不暴露** `/channels/{id}/write`（除非二次确认）。
3. UI：一个"远程协助"面板，红黄绿状态 + 一键关闭 + 有效期（到点自动停）。
4. 安全底线写进文档与 UI 提示（可参照它 `docs/安全策略与公网暴露指南.md` 的思路）：必须 token、必须 HTTPS 终止在对端、共享页只读默认。

**验收**：开启后外网能打开只读看板；关闭后端口立即不可达；token 错误返回 401 且有审计。

---

### B22 转发增强：远端 `-R`、端口段、多客户端并发

**借鉴**：`tcp_forward.h/.c` 的端口段映射（`port_base` + `/24` 内每台设备按末位偏移）与"多个监听 socket 用 poll 轮询"的做法；每个连接独立处理，不会因为一个连接慢而阻塞另一个。

**现状**（`forward.c`，155 行）：
- 只有本地转发 `L`，且解析只认 `L<port>:<host>:<port>`；
- **一个监听端口同一时刻只服务一个客户端**——`fwd_accept_thr` 在 `accept` 后进入收发循环，直到该连接结束才回去 `accept`（`forward.c:41-88`），第二个客户端会一直被内核排队；
- `libssh2_session_set_blocking(...,1)` 切阻塞建通道（`:53`），建通道期间会拖住整个 SSH 会话（reader 也在用同一会话对象）；
- 无远端转发（`-R`），而 SecureCRT/Xshell 用户高频使用"把目标机的服务映射到我本机"之外，还要"把我本机端口暴露给目标机"。

**怎么实现**

1. 拆分：`fwd_listener`（只管 accept）→ 每个连接 spawn `fwd_conn`（或用 `job` 池，上限 32）→ 连接间互不阻塞；监听线程用 `WSAEventSelect` 或 `select` 轮询所有 listener。
2. `-R`：`libssh2_channel_forward_listen_ex(sess, "127.0.0.1", port, NULL, &bound)` + `libssh2_channel_forward_accept` 循环；注意 libssh2 **同一时刻只能有一个线程操作 session**，所以 `-R` 的 accept 与转发要和 reader 用同一把"会话锁"或走非阻塞轮询（推荐后者，照 ssh-bridge "poll 不阻塞"的思路）。
3. 语法扩展（保持向后兼容）：`L 8080:10.0.0.5:80`、`R 9000:127.0.0.1:80`、`L 8001-8005:host:22`（段展开）。
4. 统计：每 listener 记 `conns/bytes/last_error`，属性页可见。

**验收**：同一本地端口并发 5 个连接都能通（`nc` 压测）；`-R` 在目标机上 `curl 127.0.0.1:9000` 回到本机服务；端口段 5 个端口一次配置全生效。

---

### B23 工程基线（严格告警 / 单测 / 质量门 / 清死代码）

**借鉴**（这些是 ssh-bridge 最"便宜"的收益）：

- `Makefile` 的严格告警集：`-Wall -Wextra -Wformat=2 -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes -Wundef`，**第三方代码单独 `-w`**（"上游的告警不是我们的标准"）；
- `version_gen.h` 构建戳 + 所有目标依赖它（防版本号不一致）；
- `tests/` 里既有 C 单测（`test_cr_hold.c`、`test_key_chars.c`、`test_script_lua.c`、`test_stress_bounds.c`）也有 Python 黑盒（`e2e_full.py`、`e2e_hot_apply.py`、`stress_web_lat.py`、`bench_ws.py`、`diag_lat.py`、`quality_check.py`）；
- `docs/C-CODING-STANDARD.md`、`CHANGELOG.md`、`.editorconfig`、`.clang-format`、`quality_gate.sh`。

**现状**：`build.ps1` 只有 `-Wall`；**零自动化测试**（`docs/测试列表.md`/`测试文档.md` 是人工清单）；无 CHANGELOG/编码规范；6 个已不在构建里的旧文件会让新维护者/Agent 读错代码（本文 0.1 已列）。

**怎么实现**

1. `build.ps1` 拆三个配置：`-DYT_RELEASE`（无 agent）、`-DYT_DEBUG`（`-O1 -g -DYT_DEBUG`）、`-DYT_AGENT`（含 `src/agent/*`）；告警集升级到上面那套（先加 `-Wno-unused-parameter` 控噪，再逐文件清）。
2. `tools/quality.ps1`：编译 → 统计告警数（>0 失败）→ 跑 `tests\*.c` 单测 → 跑 `tests\e2e_agent.py`（黑盒走 B15 接口）→ 输出 `QUALITY_REPORT.md`。
3. `tests/`：`test_ring.c`（B2/B3）、`test_key_seq.c`（B8）、`test_state_probe.c`（B9）、`test_profile.c`（B14），用 `assert` + 退出码，不引测试框架（与它一致，零依赖）。
4. 死代码：把 `src/main.c`、`src/pty_session.c`、`src/serial_session.c`、`src/session.c`、`src/term_view.c`、`src/dlg.c` 移到 `legacy/`（**先和用户确认**，不直接删；这属于搬迁不是删除）。
5. `CHANGELOG.md` + `docs/C-CODING-STANDARD.md`（可参考它的结构，内容按本项目 Win32/GDI/mongoose/cJSON 的实际情况写）。
6. 安全项：把 `profile_store.c:174-193` 的硬编码默认书签删除，改为首次启动向导；`cred_store` 的 master 导出（`cred_store.c:87-116`，单字节重复密钥 XOR）换成 PBKDF2-HMAC-SHA256 派生密钥 + AES-256-GCM 或直接"导出为 DPAPI 机器绑定的加密包 + 口令二次包裹"。

**验收**：`tools\quality.ps1` 在干净环境跑通、告警 0、单测全绿；`git status` 里没有新增的构建产物；`dist` 包内没有明文口令。

---

## 3. 不建议照搬的部分（连同理由）

| ssh-bridge 的做法 | 为什么不照搬到 YTerminal |
|---|---|
| 明文口令存 `config.json`（它自己的审计文档 `docs/代码审计与改进建议.md` 也列为 P1） | 本项目已用 **DPAPI** 加密凭据（`cred_store.c`），比它更好；要补的是"导出包用 XOR"这一处（`cred_store.c:87-116`），而不是退回明文 |
| 鉴权默认关 + `CORS: *`（为兼容存量 curl 示例而做的取舍） | 终端产品一旦带 Agent 端口，默认必须"仅回环 + 要 token"；开放必须是显式动作 |
| License / 180 天后 API 直接禁用 | 会误伤付费客户的正版使用，且终端产品没有"服务端计费"场景；不建议引入 |
| 单文件 8400 行 + 长 `if` 链路由 + 到处 `g_*` 全局状态（`http_manager.c`） | 本项目是多会话桌面程序，应按 `channel`/`agent_routes` 分文件、按会话组织状态，否则多标签下状态串味；路由用表驱动（见 D11） |
| 从日志文本里找协议字节（XMODEM 读 ACK） | 见 B11：应走 tap 旁路，否则一开时间戳/去 ANSI 就崩 |
| 业务概念耦合（board/relay/tftp/nfs 直接进通道层） | 本项目应把"通道"保持纯净（数据/状态/控制三件套），业务能力放上层 |
| Mongoose 的 GPL/商业双许可 | **本项目已在 `third_party/mongoose.c` 使用它**；分发前必须做同样的许可处理（它 `THIRD-PARTY-NOTICES.md` 的写法可参考），这一条对 YTerminal 同样成立，需法务确认 |

---

## 4. 分期实施建议

### 迭代 1（约 12 人日）：通道层正确性
`B1 → B2 → B3 → B4 → B5 → B6` + `B23`（单测骨架先立起来）。
出口标准：串口 921600 压测 0 丢字节；拔线 5s 自动恢复；大文件传输 UI 不卡；日志可导出可回放。

### 迭代 2（约 18 人日）：可用性与自动化
`B7 → B8 → B9 → B10 → B12 → B13 → B14 → B22`。
出口标准：U-Boot/登录页状态自动识别；一键进下载模式；SFTP 带进度/校验/续传；配置改完即生效且有备份。

### 迭代 3（约 25 人日）：对外能力与产品化
`B11 → B15 → B16 → B17 → B18/B19 → B20 → B21`。
出口标准：XMODEM 与真板对接成功；Agent API 有 `/help` 与锁租约；Lua 脚本可交付；局域网可发现、可共享只读；自更新可回滚。

**依赖关系**（避免返工）：
`B1` 是 `B2/B5/B6/B12/B15` 的前置；`B2` 是 `B3/B9/B10/B15/B18` 的前置；`B8` 与 `B10` 是 `B9` 动作与 `B16` 脚本的前置；`B12` 是 `B13/B16/B17/B15(exec)` 的前置。

---

## 5. 统一验收口径（可直接变成发版检查表）

| 类别 | 检查项 | 判定 |
|---|---|---|
| 正确性 | 串口 921600 传 20MB，`overrun=0`、字节数一致 | 必过 |
| 正确性 | 断线（拔 USB / 断网 10s）自动恢复，配置与转发规则保留 | 必过 |
| 正确性 | 大文件上传（500MB）期间 UI 可交互、终端不断流、有进度 | 必过 |
| 正确性 | 日志 pretty/raw 两视图正确（进度条 `\r`、彩色 ESC） | 必过 |
| 自动化 | 单测：ring / key_seq / state_probe / profile 全绿 | 必过 |
| 自动化 | `e2e_agent.py`：`/help` 示例可执行、`/lock` 互斥、`/exec` 不堵 `/health` | 必过 |
| 安全 | Release 包无明文口令、无默认可写 Agent 端口、无硬编码真实主机 | 必过 |
| 体验 | 交互延迟（发 `\r` 到回显）< 60ms 局域网 SSH / < 5ms 串口 | 目标 |
| 兼容 | 老 `sessions.json` / 老脚本 / 老 `macros.txt` 都能继续用 | 必过 |

---

## 6. 结论（一句话版）

ssh-bridge 值得借的不是它那堆板卡功能，而是**四条纪律**：

1. **通道与消费者解耦**（环形缓冲 + 单调游标 + 能力 vtable）——让终端"可被读取、可被重放、可被机器驱动"；
2. **IO 与调用方解耦**（overlapped/事件驱动读 + 写队列 + 后台 job）——让 UI 线程永远只做 UI；
3. **接口对机器友好**（自描述 `/help`、能力清单、锁租约、标记阻塞执行、审计）——让"Agent/脚本能用"变成工程能力而不是口头承诺；
4. **状态与意图分离**（`want_open` / `last_error` / 重连计数 / 阶段化更新）——让"出了事能自愈、能解释"。

把这四条落进 YTerminal 的通道层（`src/core/channel.*` + `src/core/yt_ring.*` + `src/core/job.*` + `src/agent/*`），现有 35 项看板功能里那些"占位"的（日志、编码、ZMODEM、布局、重连、脚本）会自然被补齐，而不是逐个打补丁。

---

## 附录 A：新增文件清单（含归属迭代）

| 文件 | 作用 | 对应项 | 迭代 |
|---|---|---|---|
| `src/core/channel.h` / `channel.c` | 通道能力表 + 注册/监管线程 | B1 | 1 |
| `src/core/yt_ring.h` / `yt_ring.c` | 环形缓冲、游标、时间戳、落盘 | B2 B3 | 1 |
| `src/core/byteq.h` / `byteq.c` | 发送队列（高/低优先） | B6 | 1 |
| `src/engine/serial_session.c`（**重写 reader**） | overlapped + `WaitCommEvent` | B4 B5 B7 | 1 |
| `src/engine/ssh_session.c`（**改读/写两侧**） | publish 到 ring、keepalive、写队列 | B5 B6 | 1 |
| `src/core/key_seq.h` / `key_seq.c` | 键序列解析 | B8 | 2 |
| `src/core/state_probe.h` / `state_probe.c` | 终端态识别 | B9 | 2 |
| `src/core/job.h` / `job.c` | 后台任务 + 进度 | B12 | 2 |
| `src/engine/sftp_ex.h` / `sftp_ex.c` | 分片/续传/校验/结构化目录 | B13 | 2 |
| `src/engine/xmodem.h` / `xmodem.c` | XMODEM-CRC 收发 + 串口 tap | B11 | 3 |
| `src/engine/ymodem.c` | YMODEM 批量（后续） | B11 | 3 |
| `src/agent/agent_http.c` | 路由表 + 鉴权 + 限流 + 审计 | B15 | 3 |
| `src/agent/agent_help.c` | `/api/help` 表驱动生成 | B15 | 3 |
| `src/agent/agent_lock.c` | 锁租约 | B15 | 3 |
| `src/agent/assets_gen.c`（生成物） | Web 资源字节数组 | B20 | 3 |
| `src/net/discovery.h` / `discovery.c` | UDP 发现应答 | B18 | 3 |
| `src/net/share_ws.c` | 共享会话 WS（只读/可写） | B19 | 3 |
| `src/net/tunnel.h` / `tunnel.c` | 外置隧道进程托管 | B21 | 3 |
| `third_party/lua/*` | vendor Lua 5.4 | B16 | 3 |
| `tools/updater_main.c` → `YTerminal-update.exe` | 独立更新器 | B17 | 3 |
| `tools/gen_assets.ps1` | 生成 `assets_gen.c` | B20 | 3 |
| `tools/quality.ps1` | 告警+单测+黑盒门禁 | B23 | 1 |
| `src/core/version_gen.h`（生成物） | 构建戳 | B17 B23 | 1 |
| `tests/test_ring.c` / `test_key_seq.c` / `test_state_probe.c` / `test_profile.c` | C 单测（零依赖 assert） | B2 B8 B9 B14 | 1-2 |
| `tests/e2e_agent.py` / `stress_agent.py` | 黑盒端到端/压测 | B15 | 3 |

> `build.ps1` 的 `$src` 数组需要同步追加；`tools/pack.ps1` 只需追加新增的 exe/资源，**保持不含 `src/agent/*`**。

## 附录 B：对现有文件的最小改动点（只列"必须动"的地方）

| 文件 | 改动 | 为什么 |
|---|---|---|
| `src/core/yt_core.h` | `YtSession` 增加 `YtChannel *ch;`、`line_ending/encoding/dtr/rts/auto_reconnect` 等字段；`YtWindowCfg` 增加 `log_timestamp/log_ansi/paste_mode` | 数据结构是唯一真相，先立字段再谈实现 |
| `src/core/session.c` | `yt_session_post_out` 里的 `fwrite` 删除，改调 `yt_ring_append`；`yt_session_write` 改调 `ch->write_raw`（不再持锁跨 IO） | B2 B3 B6 |
| `src/core/profile_store.c` | 用 cJSON 重写 load/save；新增 `yt_profile_patch`；删除硬编码默认书签 | B14 + 安全 |
| `src/core/window_store.c` | `yt_layout_save/load` 真正落地 `tabs[]`（可借 `sessions.json` 的 schema） | 0.3 的 YT-23 |
| `src/engine/forward.c` | accept 与连接处理解耦；加 `-R` 与端口段 | B22 |
| `src/engine/zmodem.c` | 删除 base64 伪传输分支，菜单文案改为"需目标机 rz/sz" | 消除"假成功" |
| `src/engine/sftp.c` | 只保留底层原语，上层改走 `sftp_ex` + job | B12 B13 |
| `src/ui/main.c` | 菜单项挂到 job、进度条、DTR/RTS 工具条、状态栏终端态、重连状态 | B5 B7 B9 B12 |
| `src/ui/ui_extra.c` | 属性框补齐串口 6 项 + 连接选项 | B7 B14 |
| `src/ui/term_view.c` | `term_append` 改"从 ring 取一段"（批量喂），保留现有 VT 解析 | B2 |
| `bridge/win_ui_bridge.c` | 由 `src/agent/*` 取代（保留旧路径做兼容层） | B15 |
| `build.ps1` / `tools/pack.ps1` | 三配置拆分、告警集升级、版本戳依赖、源列表追加 | B23 |
| `docs/合并功能列表.md` | 把 0.3 那 8 项状态改回 `[~]`，避免误导后续排期 | 文档诚信 |

## 附录 C：关键参数建议值（可直接写进实现）

| 参数 | 建议值 | 依据 |
|---|---|---|
| ring 容量（串口/SSH） | 2 MiB 默认，256 KiB～16 MiB 可调 | 2 MiB 约等于 115200 下 3 分钟满速输出，够"切回来还能看" |
| ring 落盘 flusher | 200ms / 64KB 先到者触发，`_IOFBF 64KB` | 与 ssh-bridge 一致，实测不卡 |
| 日志轮转 | 单文件 64 MiB 或跨天；保留 30 天 | 绿色包用户不会自己清 |
| 串口读批 | 单次 ≤8KB，循环到 `cbInQue` 为 0 | 921600 下 8KB≈87ms，留出写回余量 |
| 串口驱动队列 | `SetupComm(64KB, 16KB)` | 抗瞬时突发 |
| 重连退避 | 1→2→5→10→30s 封顶；稳定 60s 归零 | 兼顾"路由器重启 10s"与"别刷日志" |
| SSH keepalive | 15s 间隔、3 次失败判死 | 常见 NAT 超时 60s，留余量 |
| 发送队列 | 256KB/通道，用户按键不丢，脚本可丢并计数 | 溢出策略要能解释 |
| job 线程数 | 2（`job_sftp` / `job_script`） | 再多会争 libssh2 会话 |
| SFTP 分片 | 64KB/片，末片算 SHA-256 | 与 HTTP 分片同量级，进度平滑 |
| Agent 限流 | 200 req/s/通道，突发 400 | 与 ssh-bridge 同量级 |
| 锁租约默认 TTL | 60s，可 `ttl_ms` 指定，最长 1h | 防"Agent 崩了锁不释放" |
| exec 默认超时 | 5s（可指定）；输出上限 512KB | 覆盖 90% 场景 |
| XMODEM 重传 | 单块 10 次；总超时 = 2s + 0.5s/MB | 大文件不能一开始就超时 |
| Lua deadline | 30s wall-clock，可取消 | 防死循环卡住 job 线程 |
| 发现端口 | UDP 18073（避开它用的 38472/18071） | 避免同机安装两套工具互扰 |

---
---

# 第二部分 · 设计范式（D1–D17）

> 第一部分（B1–B23）回答"它有什么能力、我要补什么功能"；第二部分回答"它是怎么把这件事组织起来的、我该用哪种工程范式"。
> 这一批多是不增加功能、但直接决定体验与可维护性的东西：**很多项几十行代码，换掉一个数量级的差距**（尤其 D1、D2、D14、D16）。
>
> **一处修正**：第一部分把 TFTP / 继电器判为"弱相关"。补读 `tftp_svc.c`、`relay_ctrl.c` 后改为 **中高相关**——本项目的主场景就是"串口连板卡"，而 U-Boot 取内核需要 TFTP 服务、板子挂了需要断电重启（继电器）。这两项不是板卡业务，是**串口调试的配套工具**，已作为 D12 / D13 重写。

## 0. 第二批总览

| 项 | 设计范式 | ssh-bridge 出处 | 本项目可获得的直接效果 | 工时 |
|---|---|---|---|---|
| D1 | 1ms 定时器分辨率（带引用计数） | `util.c:176-191`、`main.c:280` | 所有 `Sleep(1)`/短超时从 15.6ms 地板降到 1ms 级 | 0.5 |
| D2 | 单调时钟 + 整数单位换算 | `util.c:105-126` | 延迟测量不被 GetTickCount 回绕与浮点精度坑 | 1 |
| D3 | 延迟埋点三层（max / 每秒 / 环形 trace） | `util.c:128-174`、`lat_trace.c:1-78`、`http_manager.c:6535-6554` | "打字为什么卡"从玄学变成可读的数字 | 2 |
| D4 | 进程生命周期（子命令/配置解析/单次 init/反向关停） | `main.c:59-208, 216-245, 269-290, 543-551`、`util.c:435-527` | 排障有入口、退出不丢数据、不留半死进程 | 2 |
| D5 | 配置差分热应用（keep/add/remove/restart） | `main.c:106-168, 390-541` | 改配置不重启、不闪断、有计数可核对 | 3 |
| D6 | 统一回复层与错误模型 | `http_manager.c:3980-4047` | 每个响应都自带安全头、缓存策略、统一错误形状 | 1 |
| D7 | 目标解析三档回退 + 路径别名 | `http_manager.c:4138-4159, 6630-6643, 6767-6777` | "只有一个通道时不用写参数"；老链接不断 | 1 |
| D8 | 异步作业模型（连接保持 + 完成回写） | `http_manager.c:8200-8329, 8414-8417` | 长任务不堵 HTTP，也不需要轮询 | 3 |
| D9 | WebSocket 通道（二进制帧/游标/同 socket 键入） | `http_manager.c:8336-8483` | 终端实时可见、按键零连接开销 | 3 |
| D10 | 资源即数据（嵌入/模板占位/主题令牌/i18n 表） | `webui_embed.c`、`http_manager.c:5113, 6570`、`themes.css`、`i18n.js` | 加一套主题/语言只改数据，不改绘制代码 | 3 |
| D11 | Agent 接口契约（三件套 + operations 表 + 工具标准） | `TOOL_STANDARD.md:171-240`、`http_manager.c:4250-4422` | "机器能用"从口头承诺变成可验证契约 | 4 |
| D12 | 串口枚举正确姿势 + 继电器控制 | `util.c:788-804`、`relay_ctrl.c:17-214` | 端口列表不再"因为被占用就消失"；一键断电重启 | 3 |
| D13 | 内置 TFTP（U-Boot 取内核） | `tftp_svc.c:35-41, 178, 400-508, 593` | 串口-only 板卡不再需要外部 tftpd32 | 3 |
| D14 | 块队列 + 最小共享层 | `util.c:700-785`、`channel_core.c:6-35` | 共享层小到一眼看完，就不会变成第二个大泥球 | 1 |
| D15 | 日志缓冲的三条硬规则 | `log_buffer.c`（全文件） | 时间戳不切 ESC、回绕可检测、调用方不被 fwrite 卡 | 2 |
| D16 | 可测的解析器（spec → 字节 + 分段表） | `key_chars.c:22-115` | 键序列可单测、可 fuzz，不依赖真串口 | 1 |
| D17 | 工程纪律与文档模板 | `C-CODING-STANDARD.md`、`DESIGN.md`、`docs/运行时总框图.md`、`tests/*` | 规范可执行、偏离可追溯、蓝图可照着实现 | 3 |

> 合计约 **36 人日**。其中 **D1 + D2 + D6 + D14 + D16 共 4.5 人日，一天内可全部落地**，建议放在第一部分任何迭代之前——先让"能测、能测准"，再谈优化。

---

## 1. 进程与时间（D1–D3）

### D1 1ms 定时器分辨率：本工程最便宜的一次性收益

**出处**：`util.c:176-191`

```c
// 注释原文：Windows defaults to a ~15.6ms timer tick; every short timeout
// (serial read with ReadTotalTimeoutConstant, select() in the HTTP poll loop,
// winpthreads condvar waits) then rounds up to that tick and the whole bridge feels laggy.
void util_hi_res_timer(int on) {
    static int depth = 0;                       // ← 引用计数：谁需要谁开，谁最后走谁关
    ...
    if (on && depth++ == 0) timeBeginPeriod(1);
    if (!on && depth > 0 && --depth == 0) timeEndPeriod(1);
}
```

**为什么这条对 YTerminal 比对 ssh-bridge 更重要**：它的通道是事件驱动（`WaitCommEvent`），定时器精度只影响边角；而 YTerminal 是**阻塞读 + 轮询**，所有延迟都被这个 tick 放大：

| 现状（`src/engine/serial_session.c`） | 默认 15.6ms tick 下的实际值 |
|---|---|
| `:200` `ReadIntervalTimeout = 1` | 间隔判定被量化到 15.6ms 附近 |
| `:202` `ReadTotalTimeoutConstant = 20` | 空闲读实际 ≈ 31ms 才返回 |
| `:52` `if (rd == 0) Sleep(1)` | 每轮空转 15.6ms，而不是 1ms |
| `:46` 16ms 批量冲刷 | 实际 15.6/31.2ms 抖动 |

全仓搜索 `timeBeginPeriod` 为 0 处命中——这正是"串口明明 115200，回显却一顿一顿"的根因之一。

**怎么实现**

1. `src/core/yt_time.h/.c` 新增 `yt_timer_resolution(int on)`，照抄它的 depth 引用计数（GUI 线程、每会话 reader、job 线程都可能各自需要，**不能各自 `timeEndPeriod`**）。
2. `WinMain` 里 `InitCommonControlsEx` 之后、创建窗口之前调 `yt_timer_resolution(1)`；`WM_DESTROY` 里 `yt_timer_resolution(0)`。
3. `local_pty.c:47`、`serial_session.c:52` 的 `Sleep(1)` 保留（1ms 现在真的约 1ms），不再需要改轮询结构——**这是给 B4 买时间**：结构改造可以后做，体感先回来。
4. 加一个自检：`tools\quality.ps1` 里跑 `tests\test_timer_floor.c`（`Sleep(1)×1000` 计时，要求 < 1.2s，否则判失败）——防止以后有人把 `timeEndPeriod` 删了。

**验收**：`Sleep(1)` 1000 次 ≤ 1.2s（改前约 15.6s）；串口 idle 时 CPU 单核占用下降；按键到回显的 P95 明显下降（用 D3 的埋点量，不靠感觉）。

---

### D2 单调时钟与"整数换算"这一个细节

**出处**：`util.c:105-126`（`util_now_us`）

它用 QPC，但**刻意先整除再取余**：

```c
/* Integer math on purpose: QPC counts since boot (~1e12 at 10MHz), and
 * converting that to double before scaling loses the low bits, which
 * quantises every measurement to ~0.5ms. */
return (c.QuadPart / freq.QuadPart) * 1000000ull +
       ((c.QuadPart % freq.QuadPart) * 1000000ull / freq.QuadPart);
```

这是"测量延迟"这件事的隐形陷阱：先转 `double` 再乘 1e6，大基数把低位吃掉，所有 0.5ms 以下的差异都被抹平——**然后你会得出"优化没效果"的错误结论**。

**现状**：YTerminal 用 `GetTickCount()` 做冲刷判据（`serial_session.c:22, 40, 46`），分辨率 15.6ms 且 **49.7 天回绕**；跨回绕时 `(now - last) >= 16` 会变负数，表现为"偶发卡一下"。

**怎么实现**

1. `src/core/yt_time.c` 提供 `uint64_t yt_now_us(void)`（QPC，照它的整数写法）与 `uint64_t yt_now_ms(void)`。
2. 全仓把 `GetTickCount()` 换成 `yt_now_ms()`（当前只有 3 处，`serial_session.c`），类型从 `DWORD` 换成 `uint64_t`——顺带消掉回绕比较的隐式有符号问题。
3. 单测：`test_time.c` 校验 `yt_now_us()` 单调、相邻两次差 ≥ 0、`yt_now_us()/1000` 与 `yt_now_ms()` 同源。

**验收**：连续运行 48h 的日志里，冲刷间隔不会出现负值或跳变；同一次操作的两次测量差值稳定在 µs 级。

---

### D3 延迟埋点：三层结构，热路径只写一个 volatile

**出处**：`util.c:128-174` + `lat_trace.c:1-78` + `http_manager.c:6535-6554`

它的做法分三层，各自成本不同：

| 层 | 实现 | 成本 | 用途 |
|---|---|---|---|
| ① 最大值槽 | `volatile uint64_t g_stat_xxx_max_us` + `util_stat_max(&slot, v)`（`:143-145`，**单写者，无锁**） | 几十 ns | 热路径常态化，永远开着也不心疼 |
| ② 每秒一行 | `EADK_STATS=1` 时把 maxima 打成一行（`:147-156`） | 1 行/秒 | 压测时看趋势，比 trace 便宜 3 个数量级 |
| ③ 环形 trace | `lat_trace.c`：262144 槽 × 16B = 4MB 固定数组，`InterlockedIncrement` 分配索引，**先写 payload、最后写时间戳**（`t != 0` 即有效），`EADK_LAT=1` 才写入 | 几十 ns/事件 | 事后还原时序，`/lat` 导出 TSV |

它埋的点也很说明问题：`read_delay`、`push_delay`、`poll`、`wspush`、`write`、`ssh_send`、`ssh_wait`、`ssh_in`——**每一个都对应一段"用户会感觉到卡"的链路**，而不是笼统的"总耗时"。

**现状**：YTerminal 只有状态栏的 CPU/RAM（`main.c` 状态栏），没有"按键→屏幕"的链路测量；所以"优化了但没感觉"和"感觉卡但说不出哪一段"都无法裁决。

**怎么实现**

1. `src/core/yt_stat.h`：一组 `volatile uint64_t g_stat_*_max_us`，命名对齐真实链路：
   `key_in`（按键进窗口）→ `write_enqueue`（入发送队列）→ `dev_write`（WriteFile 完成）→ `rx_read`（读到字节）→ `ring_append` → `post_ui`（PostMessage）→ `paint`（WM_PAINT 结束）。
2. `YT_STATS=1` 时由 UI 的 300ms 定时器打一行：`read=1.2ms write=0.4ms post=2.1ms paint=6.8ms`。
3. `YT_LAT=1` 时启用 `src/core/lat_trace.c`（照它的槽结构与 `t` 写法），`/stats` 接口导出最近 N 条 `(ts, code, arg)`；`arg` 用 `tab_index<<16 | len`，这样能把"哪个标签、多少字节"带出来。
4. **不要在 reader 线程开 trace**（它注释里明确写了这条），只开 max 槽。

**验收**：`YT_STATS=1` 下连续打字 1 分钟，能读出"卡在 paint 还是 read"；`/stats` 导出可被 Excel/脚本直接分析；关掉开关后热路径无额外开销（对比开/关的 CPU 差异 <2%）。

---

## 2. 配置与生命周期（D4–D5）

### D4 进程生命周期：把"排障入口"做进 exe

**出处**：`main.c:59-208`（usage/version/which-config）、`:216-245`（子命令分派）、`:269-290`（过期仍启动、只禁 API）、`util.c:435-527`（配置查找/创建）、`:26-42`（Ctrl+C 两次退出）、`:543-551`（关停顺序）

四个可迁移的设计：

**(1) 子命令**：`help | version | which-config | update`，`version` 在"功能被禁用"时**返回码 2**（`:221-223`）。这样脚本能区分"版本旧了"和"启动失败"。

**(2) 配置解析失败要"自解释"**：`report_missing_config`（`:191-208`）不只说"not found"，而是把**试过的三条路径逐条打出来**，并给出"放这里 / 用 -c 指定"的做法。对比多数程序只丢一句 `failed to load config`。

**(3) 配置搜索顺序 + 归属戳**（`util.c:400-527`、`TOOL_STANDARD.md:144-159`）：

```
1) <cwd>/<tool>_config.json
2) <exe_dir>/<tool>_config.json      ← 双击启动时 cwd 常常不是 exe 目录，必须查
3) <用户目录>/.<tool>/<tool>_config.json
4) 同序再查遗留名 config.json
缺失时"创建默认配置"而不是直接失败；根级写 "tool":"<id>" 归属戳
读文件时若 tool 不符 → 跳过（防两个工具互相覆盖同一份 config.json）
```

**(4) 关停顺序 = 启动的反向**（`:543-551`：inbound → discovery → http → bridges → config → net）。原则是"先停生产者、再停消费者、最后停网络"——先关 HTTP，外部就不会在半死状态继续打请求进来。

**现状**（`src/ui/main.c:1881-1895`）：`WinMain` 里 `(void)prev; (void)cmd;`——**命令行参数被完全忽略**，没有 `--version`、没有 `--which-config`、没有"诊断模式"；配置全在 exe 同目录（`paths.c:11-31`，`<exe>\config`），装到 `Program Files` 就写不进去；网络/加密库是**每个会话各自 init**（`ssh_session.c:229-230` `WSAStartup` + `libssh2_init`），全仓无 `WSACleanup`/`libssh2_exit`。

**怎么实现**

1. **CLI 分两段**：GUI 子系统下 `AttachConsole(ATTACH_PARENT_PROCESS)` 拿父控制台；拿不到就 `AllocConsole` 并 `freopen` stdout。参数解析放在创建窗口之前，`--version/--which-config/--diagnose` 直接 `return`（不建窗口）。
2. 子命令建议：`--version`（含 build 戳）、`--which-config`（打印解析结果和候选列表）、`--diagnose`（打印 COM 列表、DLL 版本、libssh2 版本、最后 50 行日志路径）、`--safe-mode`（不加载会话、只开空窗口——用户被坏配置卡死时的逃生门）。
3. 配置目录策略：**便携优先，写不进去就回落**。启动时对 `<exe>\config` 做一次写探针（建临时文件再删）；失败则用 `%APPDATA%\YTerminal\config`，并在标题栏/关于框显示当前实际路径。**不要**静默改路径。
4. 全局单次 init：`yt_net_init()`（`WSAStartup`）+ `libssh2_init` 放 `WinMain` 开头，`libssh2_exit` + `WSACleanup` 放关停尾部；`ssh_session.c:229-230` 那两行删掉。
5. 关停顺序固定成一张表并写进注释：`bridge(HTTP) → 各 tab 会话 → job 线程 → 落盘(日志/布局/凭据) → GDI 资源 → net 清理`。当前 `WM_DESTROY`（`:1859-1869`）是"先保存、再关 tab、再停 bridge"，顺序应调整为"先停 bridge（断外部请求）→ 关 tab → 保存"。
6. 退出保护：有未完成任务（SFTP 传输中/脚本运行中）时提示"还有 N 个任务，确定退出？"，并给"等它完成/强制"两选项——比默默丢掉半个文件好。

**验收**：`YTerminal.exe --which-config` 在三种目录布局下都打印正确路径并退出码 0；`--diagnose` 输出可直接贴进 issue；从 `Program Files` 启动不报权限错误；进程退出后 `handle.exe` 无残留 COM 句柄。

---

### D5 配置"外科手术式"热应用：不重启、不闪断、有账可对

**出处**：`main.c:106-168`（critical vs soft 判定）、`:390-541`（差分替换）、`:536-537`（计数汇总）

这是它最值得抄的一段工程实现。核心三点：

1. **按 `name` 匹配** desired（新配置）与 current（运行中），把结果分成四类：`keep`（保留实例）/ `added`（新建）/ `removed`（待拆）/ `restarted`（必须重建）。
2. **字段分两级**：`board_critical_changed`（`:106-121`）只把"改了就活不成"的字段算 critical（port/baud/databits/stopbits/buffer/auto_open/serial_device/parity/flow/user/pass/log_dir）；其余走 `soft_apply_serial`（`:138-153`）——把 patch JSON 通过 `control("configure")` 热送进实例。**能热改的绝不重建**。
3. **先换表、再拆旧**（`:519-529`）：

```c
http_manager_set_bridges(http, next, nn);   // HTTP 先看到新表 → 不存在"通道消失"的窗口
...
for (i...) { doom[i]->stop(); bridge_destroy(doom[i]); }   // 之后才拆退役的
```

最后打一行账：`surgical apply done: kept=%d added=%d removed=%d restarted=%d total=%d`——**运维凭这一行就能确认"我的修改到底生效成了什么样"**。

**现状**：YTerminal 的属性对话框保存后，改动要么"下次启动生效"（`window_store`/`profile_store` 落盘），要么需要关闭再开标签；没有"保留实例、只热改字段"的概念，也没有任何"应用结果回执"。

**怎么实现**

1. `src/core/session_patch.h/.c`：

```c
typedef enum { YT_PATCH_KEEP, YT_PATCH_RESTART, YT_PATCH_ADD, YT_PATCH_REMOVE } YtPatchKind;
YtPatchKind yt_session_diff(const YtProfile *cur, const YtProfile *want, char *why, int cap);
```

`why` 写明是哪一项触发了 restart（"baud 115200→921600"），供 UI 直接显示。

2. 分级表（照它的思路，按本项目字段）：

| 级别 | 字段 | 处理 |
|---|---|---|
| critical（必须重建） | kind、host、port、user、keyfile、jump_*、com、baud、data/stop/parity/flow | 关旧会话→建新会话，保持 tab 位置与标题 |
| soft（热改） | name、log_on、timestamp、paste_delay_ms、font_px、theme、sync_input | 直接改内存 + 重绘，不碰连接 |
| 只落盘 | 备注、分组、排序 | 不触发任何动作 |

3. 应用流程照它的顺序：**先更新"会话表/UI 数据源" → 再重建 critical 的那几个 → 最后销毁退役的**；任何一步失败都保留旧实例并回滚表（它的 `if (b->start(b)==0)` 检查就是这个意思）。
4. UI 回执：属性框"确定"后弹一行非模态提示——`已应用：保留 2、重建 1（波特率变更）、新增 1`；失败则说明原因并保持原状。

**验收**：改"日志开关/字号/主题"零重建（会话不闪断、SSH 不掉线）；改波特率只重建该 tab；批量导入 5 个会话时，原有 3 个会话不中断；每次应用都有"保留/重建/新增/移除"计数。

---

## 3. 接口与传输（D6–D9）

### D6 统一回复层：让每个响应都自带"安全 + 缓存 + 错误形状"

**出处**：`http_manager.c:3980-4047`

```c
#define SECURITY_HEADERS \
    "X-Content-Type-Options: nosniff\r\n" \
    "X-Frame-Options: DENY\r\n" \
    "Referrer-Policy: no-referrer\r\n"

json_reply(c, 200, ...)  // Content-Type + Cache-Control: no-store + CORS + 安全头，一处定义
err_json(c, 404, "unknown board/link: %s");   // 所有错误一个形状：{"error":"..."}
reply_api_disabled(c);   // 被禁用时返回结构化原因（expired/api_enabled/version/releases），而不是裸 403
```

三个细节值得学：

- **安全头集中定义**，不是每个 handler 各写一遍（漏一个就少一个）。
- **`Cache-Control: no-store`**：本机工具的状态接口绝不能被浏览器/代理缓存，否则 UI 显示的是旧状态。
- **"拒绝"要自解释**：功能不可用时返回"为什么不可用 + 怎么办（版本/过期时间/下载地址）"，而不是一个空 403。

**现状**：`bridge/win_ui_bridge.c` 的响应是手写 `mg_http_reply`，没有安全头、没有 `no-store`、错误体是 `{"error":...}` 但只在少数分支出现；`429` 那条只给了 `busy: another action is in flight`，没说"谁在飞、等多久"。

**怎么实现**

1. `src/agent/http_util.h/.c` 只暴露四个函数：`yt_reply_json/text/html/err`，安全头与 `no-store` 写在里面。
2. `yt_reply_busy()` 返回 `{error:"busy", holder:"<action>", since_ms:1234, retry_after_ms:200}` + `Retry-After` 头——把"忙"变成可执行的信息。
3. `yt_reply_feature_off(reason, hint)` 统一"功能未开启/未授权/需要付费"三类拒绝。
4. 全局兜底：路由未命中 → `yt_reply_err(404, "no such endpoint; GET /api/help")`——**错误信息里直接给出下一步**。

**验收**：`curl -I` 任意接口都能看到 3 个安全头 + `no-store`；所有错误响应都是同一个 JSON 形状；`/api/help` 里列出的路径与真实路由一一对应（可用脚本比对）。

---

### D7 目标解析：三档回退 + 路径别名

**出处**：`http_manager.c:4138-4159`（`select_bridge`）、`:6630-6643`（`/tcp_forward/*` 与 `/tcp-forward/*` 双写）、`:6767-6777`（`/docs/x`、`/help/x`、`/share/tools/x` 三名同物）

```c
/* 明确的 link > board/name > 只有 1 个通道时隐式选中 > 报错并说明缺什么 */
if (m->n == 1) b = m->bridges[0];
else snprintf(err, ..., "board or link parameter is required when multiple bridges are configured");
```

这条设计解决的是"AI 写 curl 时最常犯的错"：多通道时必须点名，单通道时**不该逼你编一个名字**。而报错文案直接把"你缺什么参数"说出来。

**现状**：YTerminal 的桥接只有 `/action` 一个写入口，参数里带 tab 索引；没有"名字"概念（会话只有 `name` 字段但没有稳定 id），也没有别名兼容。

**怎么实现**

1. 给会话一个稳定 `id`（`name` 可改，`id` 不可改；导入时生成 `s1`、`s2`…），同时接受 `id` / `name` / `tab_index` 三种寻址。
2. 解析函数 `yt_resolve_target(req)` 按"精确 id → 精确 name（大小写不敏感）→ 唯一会话 → 报错"四档；报错文案模板：`multiple sessions; pass ?id= (available: dev-board, lab-2)`——**把候选列出来**。
3. 路径别名：`/channels/{x}/write` 与 `/sessions/{x}/write` 同时注册（表驱动里一个 entry 两个 path），旧路径保留到下一个大版本。

**验收**：单会话时 `POST /write` 不带参数成功；多会话时不带参数返回 400 且 body 里列出候选；`id` 与 `name` 都能寻址；旧路径仍可用。

---

### D8 异步作业模型：连接保持 + 完成回写，而不是轮询

**出处**：`http_manager.c:8200-8329`（写入返回 `202` + `queued:1` + `pending_commands`）、`:8414-8417`（`MG_EV_WAKEUP` 里 `job_try_complete`）

它的语义是这样：

| 操作 | 立即返回 | 完成方式 |
|---|---|---|
| 写字节/发命令（短） | `202 {queued:1, pending_commands:N}` | 不需要回复，靠日志流看结果 |
| 上传/脚本/XMODEM/宿主命令（长） | 挂起该连接 | 后台线程做完 → 唤醒 poll → **在同一条连接上回 `200` + 结果** |

优点：客户端只发一次请求就拿到结果，不用轮询、不用存 job_id；缺点：慢网络/客户端超时会拖住连接，且**没有 `/jobs/{id}` 兜底**（这是它没做、而本项目该补的）。

**现状**：YTerminal 的 `/action` 是**同步执行 + 单飞（第二个直接 429）**（`win_ui_bridge.c:585-586`），长动作（导入、导出、SFTP 列表）会把整个桥接堵死。

**怎么实现**

1. `src/core/job.c`（B12 已列）加 `yt_job_submit(work, done, ctx)`；HTTP 侧拿到 job 后**先回 `202 {job_id, queued:1}`**，把连接释放——本项目面向 GUI+Agent 混合，不该让连接一直挂着。
2. 补 `GET /jobs/{id}`（`{state:queued|running|done|failed, progress, result, error}`）与 `DELETE /jobs/{id}`（取消），这样 Agent 可以"发一次、等一会儿、查结果"，容忍网络抖动。
3. 长任务上限：同时 4 个 job（SFTP 2 / 脚本 1 / 导入导出 1），超出返回 `429` + `Retry-After`。
4. 完成通知：job 结束 `PostMessage(WM_YT_JOB_DONE)` 给 UI，UI 只做"更新进度条/弹提示"，**不做实际工作**。

**验收**：上传 500MB 期间 `/health` 的 P95 < 50ms；`/jobs/{id}` 能查到 `progress`；两个 job 并行不互相阻塞；取消能真正停下（检查线程退出码）。

---

### D9 WebSocket 通道：二进制帧 + 游标 + 同 socket 键入（并补上它缺的背压）

**出处**：`http_manager.c:8336-8483`

它的实现里有五个关键判断：

1. **二进制帧**（`:8365-8366`）：注释直白——"boot logs are not always valid UTF-8; TEXT would kill the WS"。串口/内核日志里有非法 UTF-8 字节，用 TEXT 帧会让浏览器直接断开连接。
2. **订阅时把游标钉在流末尾**（`:8472-8478`）：`get_log_since(..., &epoch, &seen, ...)` 丢掉历史窗口——新开的页面从"现在"开始，不会先灌 2MB 旧日志。
3. **`notify_seq` 跳过空轮询**（`:8354-8357`）：没有新数据就 `continue`，避免每次 poll 都拼一次字符串。
4. **同一个 socket 收键入**（`:8391-8413`）：注释说明了动机——"avoid per-key HTTP connect/accept, which Windows select quantises to ~15.6ms"。**每按一个键建一次 TCP 连接**在 Windows 上就是 15.6ms 起步，直接用已建立的 WS 发键才可能做到"跟手"。
5. **订阅跟随通道生命周期**（`:8346-8351`）：通道被热移除时，把订阅置 `is_draining` 断开，而不是留一个野指针。

**它缺什么（本项目要补）**：`ws_push_logs` 里 `mg_ws_send` 之后**没有检查 `c->send.len`**。慢客户端（比如手机弱网看日志）会让 mongoose 的发送缓冲一直涨——这是内存型的稳定性隐患。

**现状**：YTerminal 没有 WS，桥接只有一次性 GET。

**怎么实现**

1. `src/agent/ws_term.c`：`GET /ws/term?id=&tail=0|N&strip=0|1` → 升级；首帧可选回填 `tail` 字节（默认 0，对应它的"钉尾"语义）。
2. 推送循环挂在 UI 的 100ms 定时器上（**不要在 reader 线程推**）：`yt_ring_since(&epoch,&seen)` 取增量 → 二进制帧发出。
3. 键入：`WS_MSG` 收到就 `yt_session_write()`，但**先过 `read_only` 与锁检查**（照它 `:8398-8410`）：会话被锁定/被其他 Agent 持锁时只允许读。
4. **加背压**：`if (c->send.len > 256*1024) { g_stat_ws_drop++; return; }`，并在下一帧发一个小控制消息告诉前端"你落后了 N 字节"。宁可丢中间帧，不能让进程内存涨。
5. 关停：`WM_DESTROY` 前把所有 WS 置 draining，等 200ms 再退出。

**验收**：`printf '\xff\xfe'` 这类非法 UTF-8 输出不会导致连接断开；新开页面 0 延迟显示新数据；按住方向键连续发键，端到端延迟 < 30ms（局域网）；弱网模拟（限速 100KB/s）下进程内存不增长。

---

## 4. 资源与契约（D10–D11）

### D10 资源即数据：嵌入、模板占位、主题令牌、语言表

**出处**：`tools/gen_webui_embed.py` → `src/webui_embed.c`（49623 行生成物）、`http_manager.c:5113`（`load_webui_template`）、`:6570-6575`（`str_replace_all(html, "__HTTP_PORT__", ...)`）、`themes.css`（5936 行 / 344KB，约 150 套）、`i18n.js`（904 行，20 种语言 + 别名表）

四个机制的共同点：**"表现"全部是可替换的数据，代码只负责"取和套"**。

| 机制 | 它怎么做 | 迁移到 YTerminal |
|---|---|---|
| 嵌入 | 构建期把 `webui_static/*` 生成 C 字节数组，单 exe 无外部依赖 | `tools/gen_assets.ps1` 把 `ui/help/*.html` 生成 `assets_gen.c`（B20） |
| 模板占位 | 服务时替换 `__HTTP_PORT__` 等 | 帮助页/关于框替换 `{{version}}`/`{{config_path}}`/`{{port}}` |
| 主题令牌 | `data-theme="id"` + 一组 CSS 变量（`--line/--text/--muted/--accent/--card-bg`） | **GDI 侧同样可以做**：`YtTheme` 结构体 12 个颜色令牌 + 预设 4-6 套 + `themes/*.json` 热加载 |
| 语言表 | `i18n.js` 的 key→字符串表 + `auto` + 别名（`zh-hans→zh-CN`） | `src/core/strtab.c`：`yt_tr("menu.file")`，zh/en 两张表，缺 key 回落中文 |

**但要注意它的过度设计**：150 套主题（含 `win98-classic`、`dos-prompt`、`math-pi`、十二星座…）是**产品玩笑 + 维护债**，`themes.css` 已 344KB。本项目应**学机制、不学数量**：先 4 套（浅色、深色、高对比、Solarized 类），每套只定义令牌，不写"某主题下按钮要圆角 999px"这种特例（它的特例规则占了大半文件）。

**现状**：YTerminal 主题是 `int 0/1`（`main.c:179`），绘制里散落 `COL_*` 宏；`themes/` 目录已创建（`paths.c:22,27`）但**没有任何加载逻辑**；UI 字符串全是 `L"..."` 字面量，无法切换语言。补充一个背景：git 工作区里 `src/theme.c`、`src/theme.h` 是 tracked-deleted——**旧版有过独立主题模块，这次重写把它丢了**，所以 D10 同时也是"低成本补回一个已存在过的能力"。

**怎么实现**

1. `src/core/theme.h/.c`：`YtTheme { bg, bg_alt, fg, fg_dim, accent, line, sel_bg, sel_fg, ok, warn, err, ansi[16] }`；`yt_theme_load(id)` 读 `themes/<id>.json`，缺字段回落默认；`yt_theme_apply()` 只 `InvalidateRect`，不重建窗口。
2. 把绘制代码里的 `COL_CHROME` 等宏替换成 `yt_theme()->chrome`（机械替换，风险低）。
3. `src/core/strtab.c`：`yt_tr(id)` + `yt_lang_set("zh-CN"|"en"|"auto")`；只翻译**菜单/对话框/状态栏**（约 200 条），终端内部输出不翻译。
4. 语言/主题切换后**不重启**，当场重绘/重建菜单文本。

**验收**：新增一套主题只加一个 JSON 文件，0 行代码改动；切主题不闪屏、不乱码；切成 `en` 后菜单/对话框全英文、终端内容不受影响；缺 key 不显示空白（回落中文）。

---

### D11 Agent 接口契约：三件套 + operations 表 + 工具标准

**出处**：`TOOL_STANDARD.md:171-240`、`http_manager.c:4250-4422`（`/api/help` 由 operations 表驱动、用真实实例数据拼 curl 示例）、`:6358`（`/help` 的 Markdown 正文）、`:4049-4078`（`/health`）

`TOOL_STANDARD.md` 定了一份**产品级契约**，这是 YTerminal 的桥接最缺的东西：

| 端点 | 类型 | 硬性要求 |
|---|---|---|
| `GET /health` | JSON | `{ok, tool, product, version, build_utc, http_port, ...}`，**永远 200**（进程活着就是活着） |
| `GET /api/help` | JSON | `{tool, version, http_port, operations:[{id, method, path, query, side_effect, summary}]}`，**机器读这个** |
| `GET /help` | `text/plain` | Markdown 手册，命令**单独成行**（不用代码围栏，便于三击复制），文首给三条链接 |
| `GET /` | HTML | WebUI，必须有指向 `/help` 的链接，文字就叫 **「Agent 接口」** |

配套的三条纪律，比端点本身更重要：

1. **Skill 只教"先读 `/api/help`，再按 operations 调用"**，禁止写死 help 里没有的路径——这样接口加了新能力，Skill 不用改。
2. **`/api/help` 用当前实例的真实数据拼示例**（`:4250-4422`）：示例里出现的是真实通道名、真实端口，复制即用——而不是 `{board}` 占位符让人猜。
3. **错误形状统一**：`{ok:false, error:{code, message}}`；`side_effect:true` 的写操作必须在 help 里标注。

**现状**：YTerminal 桥接 5 个端点（`/ui-tree`、`/state`、`/logs`、`/action`、`/ping`），**没有 `/health` 的版本信息、没有 `/api/help`、没有 `/help`、没有 WebUI**；token 空则全放行（`win_ui_bridge.c:563`）；且 Release 包根本不编译它（`pack.ps1`）。

**怎么实现**

1. **一张表管三处**（这是它最值钱的思想）：

```c
typedef struct {
    const char *id, *method, *path;      // "session.write", "POST", "/channels/{id}/write"
    int side_effect;
    const char *summary, *example_fmt;   // example 用真实数据格式化
} YtOp;
static const YtOp g_ops[] = { ... };
// 路由、/api/help、/help 正文、e2e 测试的用例表 全部从 g_ops 生成
```

2. `src/agent/yt_ops.c` 定义表；`agent_http.c` 按表匹配（**不要长 if 链**，那是它 8400 行的主要来源）；`agent_help.c` 按表生成两个帮助页。
3. `<help>` 的写法照它：`§1 约定`（Base、单/多目标选择、PowerShell 用 `curl.exe`）、`§2 字段说明`、`§3 典型操作`（命令紧挨说明、命令之间空行）、`§末 速查表`（与表一致，不含未实现路径）。
4. **配置归属戳**：`config/*.json` 根级加 `"tool":"yterminal"`，读时校验，不符就跳过（防用户把它和别的工具混在一个目录里互相覆盖）。
5. 端口与命名：固定 `127.0.0.1:18760`（避开 ssh-bridge 的 18081、发现 18071/18073），绑定非回环必须显式开关 + token 双条件。

**验收**：`GET /api/help` 的每条 `path` 都能被 `curl` 打通（脚本自动验证）；`/help` 里每条示例复制即可执行；表里新增一个 operation，`/api/help`、`/help`、测试用例同时出现（不需要第二处手改）；token 为空时只允许回环访问。

---

## 5. 设备面（D12–D13）

### D12 串口枚举的正确姿势 + 继电器控制

**出处**：`util.c:788-804`（枚举）、`relay_ctrl.c:17-38`（A0 帧）、`:143-214`（set/pulse/run_mode）

**(1) 枚举不能用"能不能打开"来判断**

```c
/* ssh-bridge：查设备映射表，不打开端口 */
if (QueryDosDeviceA(name, target, sizeof(target)) != 0) { ... }   // COM1..COM256
```

YTerminal 现在是**逐个 `CreateFileW("\\\\.\\COM%d")` 试开**（`features.c:5-20`）：

- 端口被别的程序（比如已经开着的标签页、外部串口助手）占用时，`CreateFile` 失败 → **该端口从列表里消失**，用户以为设备掉了；
- 打开串口会拉 DTR/RTS，某些 USB 串口板（CH340/CP210x 接复位电路）会**顺手复位板子**——"我只是打开下拉框看看端口，板子怎么就重启了"。

顺带说：同一个仓库里已经有一份**正确做法**——`ui_extra.c:36-55` 的 `fill_com_ports` 用 `QueryDosDeviceW(NULL, ...)` 枚举全部 DOS 设备名再筛 `COM\d`，**完全不打开端口**。但 `features.c:5-20` 的 `yt_com_enum` 是"逐个试开"，两套实现并存且行为不一致：属性框里能看到被占用的端口，快捷连接里却看不到。统一到 `QueryDosDevice` 是零风险的改动。

**(2) 继电器：把"断电重启"变成一个按钮**

`relay_ctrl.c` 的设计非常克制，正好够用：

- 帧格式 `0xA0 | channel | on/off | checksum`（`:24-33`，checksum = 前三字节和取低 8 位）；
- **逻辑电平与线电平分离**（`logical_to_wire`，`:35-38`）+ **脉冲极性分离**（`polarity_inverted`）——同一套代码兼容"高电平有效/低电平有效"的继电器板，不用改硬件；
- 三种模式：`hold_on` / `hold_off` / `pulse`（`:202-214`），`pulse` = 拉 `hold_seconds` 再回 idle（`:184-200`）；
- 每次操作"开→写→等 `post_write_delay_ms`→关"，**不长期持有句柄**（`:143-182`）——继电器是低频设备，没有必要占用一个常驻会话。

**现状**：YTerminal 无继电器概念；`ui_extra.c` 的串口设置只有 COM + 波特率（B7 已列）。

**怎么实现**

1. 枚举：`src/core/serial_ports.c` 用 `QueryDosDeviceW(NULL, ...)` 遍历（照 `ui_extra.c:36-55` 的做法），再从注册表 `HKLM\HARDWARE\DEVICEMAP\SERIALCOMM` 拿"设备名→COM 号"映射，进一步读 `HKLM\SYSTEM\CurrentControlSet\Enum\USB\...` 的 `FriendlyName`（"USB-SERIAL CH340 (COM3)"）——**显示人能认的名字**，比它只给 `COM3` 更好。注意 `fill_com_ports` 在"查不到"时会硬塞 `COM1..COM20`（`:49-54`），这会给出不存在的端口，新实现**不要保留这个兜底**。
2. `src/engine/relay.h/.c`：照它的三函数签名（`set/pulse/run_mode`），加一个 `yt_relay_configure(port, channel, io_inverted, polarity_inverted, hold_ms, post_delay_ms)`。
3. UI：会话右键菜单/工具条加"断电重启(2s)"；设置页加"继电器设备"（端口/通道/极性/保持时长）——**继电器是设备，不是标签页**，不要为它开一个终端。
4. 与 B9 联动（这是它没想到、而本项目最该做的）：`断电 → 等 3s → 轮询 probe("U-Boot"/"login:") → 命中后自动执行预设命令序列（如 `setenv bootargs...; boot`）`，一条宏完成"板子挂了自动救回来"。

**验收**：串口被占用时仍出现在下拉框（标注"占用中"）；插入 CH340 后列表 1s 内刷新；点"断电重启"后 5s 内板子回到 U-Boot 提示符；极性接反时勾一个选项就能用。

---

### D13 内置 TFTP：串口-only 板卡的最后一块拼图

**出处**：`tftp_svc.c:35-41`（opcode/BLK=512）、`:178`（`tftp_safe_join`）、`:400-452`（RRQ）、`:454-507`（WRQ）、`:509`（每请求一线程）、`:593`（"tftpd-hpa often owns :69" 的提示）

**为什么第一部分判错了**：当时的理由是"YTerminal 是终端，不是文件服务器"。但补读后看清它解决的真实场景——**板卡只有串口、没有网络或网络还没起来时，U-Boot 用 `tftp` 命令取内核/设备树/rootfs**。此时的完整链路是：串口看 U-Boot 提示符 → 串口敲 `tftp 0x82000000 zImage` → **需要一台 TFTP 服务器**。用户的现状是"再开一个 tftpd32"，多一个软件、多一个配置点、多一个"IP 填错"的坑。

它的实现细节值得抄的四处：

1. **路径安全**（`:178`）：`tftp_safe_join(root, rel)` 只允许 root 下的相对路径，挡掉 `../` 穿越——**这是唯一的攻击面**（RRQ 文件名来自网络）。
2. **每请求独立 socket**（`:509` + `:400`）：TFTP 规范要求服务器用**临时端口**回 DATA，不是从 69 回——共用 socket 会和并发请求串台。
3. **重传 + 超时**（`:438` 收 ACK 2s、`:489` 收 DATA 5s）：块号错/超时就重发上一块，而不是断开。
4. **端口占用的可执行提示**（`:593`）：绑 69 失败时说清"可能被 tftpd-hpa 占用"，而不是 `bind failed`。

**现状**：YTerminal 无任何文件服务；用户只能外挂 tftpd32。

**怎么实现**

1. `src/net/tftp.h/.c`：**先只做 RRQ（下行）**，WRQ 二期（上行需要更多校验）。`SOCK_DGRAM`，主 socket 绑 69（可改 6969），每请求起一个短线程/或用同一 select 循环 + 每请求临时 socket。
2. 共享目录：`<exe>\share`（可配），UI 里一个"文件共享"面板：列文件（名字/大小/修改时间）、拖入即上传（写 `share/`）、显示"谁在拉什么"（`10.0.0.5 请求 zImage 3.2MB 完成`）。
3. 与串口联动：U-Boot 里输入 `tftp` 后，面板顶部显示**建议命令**（按当前 IP/文件名生成 `tftp 0x82000000 zImage; bootm`）——把"填 IP"这步省掉。
4. 安全默认：**只在用户点了"启动共享"后才绑端口**；默认只允许同网段（用请求源 IP 与本地网卡网段比对）；提供"只读"开关。
5. 校验：面板显示文件 MD5，U-Boot 侧 `md5sum` 可比对（它 `tftp_svc.c` 里的 md5 实现可直接复用思路）。

**验收**：真板 U-Boot `tftp 0x82000000 zImage` 成功启动；`../../etc/passwd` 请求被拒（返回 ERROR 码）；两个板子同时拉不同文件互不串；69 被占用时提示明确并自动回落 6969。

---

## 6. 缓冲与日志（D14–D16）

### D14 块队列 + 30 行的"最小共享层"

**出处**：`util.c:700-785`（`ByteQueue`）、`channel_core.c:6-35`

两个设计判断：

1. **队列用"块链表"而不是定长环形**（`ByteQueueNode` 存 `data/n/nxt`）：发送队列的元素是"一次写入的完整意图"（一条命令 + 换行），块链表天然保序、天然支持"整块丢弃"（队列满时丢最老的块并计数）；环形则要把消息边界另存一份。
2. **共享层只有 36 行**（`channel_core.c`）：`init`（建 log_buffer + 存 kick 回调）/`on_rx`（append + kick）/`notify_seq`/`fini`。**它没有把 serial 和 ssh 的差异硬塞进共享层**——共享层只做"日志 + 通知"，其余各写各的。

这条经验直接对应本项目最大的风险：B1 的 `YtChannel` vtable 很容易越写越大（重连策略、编码、行尾、流控全塞进去），最后变成一个"什么都管、什么都管不好"的上帝对象。

**现状**：YTerminal 的发送缓冲是 `SerImpl.wbuf`（`serial_session.c:95-99`，裸 `realloc` 线性缓冲 + memmove），没有优先级、没有字节计数、没有"满了怎么办"的策略。

**怎么实现**

1. `src/core/byteq.h/.c`：块链表 + `yt_byteq_push/pop/size/drop_oldest/wakeup`；每块记 `len` 与 `prio`（用户按键 prio=0 不丢，脚本/日志 prio=1 可丢）。
2. `src/core/channel_core.h/.c` 严格限制在"ring + kick + notify_seq + 状态字段"；**凡是需要 `if (kind==SERIAL)` 的逻辑，一律不许进这个文件**（这条写成代码注释）。
3. 代码评审规则：`channel_core.c` 超过 80 行就说明有人往里塞业务了。

**验收**：`byteq` 单测覆盖"满→丢最老→计数正确"；`channel_core.c` 行数 < 80；新增一种通道类型（如 Telnet）不改 `channel_core.c` 一行。

---

### D15 日志缓冲的三条硬规则

**出处**：`log_buffer.c`（682 行，全文件），规则集中在时间戳插入与游标语义

| 规则 | 它怎么做 | 为什么必须这样 |
|---|---|---|
| ① 时间戳只插在**行首**且**ANSI 状态为 0** 时 | 逐字节扫，遇 `\n` 置"下一字节是行首"，遇 `ESC[` 进入 ANSI 态直到终止符；只在两者都满足时插 `[HH:MM:SS.d] ` | 否则会把 `ESC[32m` 从中间切开，终端显示成乱码 |
| ② `\r` 要"挂起"（`cr_hold`） | 收到 `\r` 不立刻决定，等下一个字节：若是 `\r\b` 或普通字符再判 | 进度条用 `\r` 覆盖同一行，直接插时间戳会把进度条打散 |
| ③ 游标是 `(epoch, seen)` 而非字节偏移 | ring 回绕时 `epoch++`；读方带旧 epoch 来读 → 明确告知"你落后了，已回绕" | 字节偏移在回绕后会"看起来还在范围内"，读到错位数据 |
| ④ 调用方不 `fwrite` | 独立 flusher 线程 + `setvbuf(_IOFBF, 64KB)`；`append` 只进 ring | 否则串口 reader 会被磁盘 IO 卡住（本项目现在正是这样：`session.c:27-30` 在收数据路径里 `fwrite`） |
| ⑤ 运行中可开关落盘 | `set_log_disk(on)` 重启 flusher，不清空已有 ring | 用户"刚才那段没存下来，现在开始存"不用重开会话 |

**现状**：YTerminal 的日志是 `session.c:27-30` 的裸 `fwrite`（无时间戳、无 ANSI 感知、无游标），文件名 `tab%d.log`（`main.c:1562-1564`）——重启就覆盖/接续混乱。

**怎么实现**

1. B2/B3 的 `yt_ring` 里实现①②③（它们是 ring 的属性，不是"导出时"的属性——**时间戳必须在入 ring 时就决定**，否则增量读会重复插）。
2. flusher 独立线程，200ms/64KB 先到者触发；文件名 `<session>_YYYYMMDD.log`；跨天自动换文件。
3. `set_log_disk` 语义照它：开→起 flusher 并从当前游标继续写；关→flush 并停线程；**不清 ring**。
4. 导出/下载时再决定 `strip=1` 去 ANSI（**不要**在存储时去，否则回看彩色日志就没了）。

**验收**：`printf 'ESC[32mgreenESC[0m\n'` 落到日志里 ESC 序列完整；`\r` 进度条在日志里是"多行"而不是"一行糊掉"；连续输出 20MB 后旧 epoch 的读请求返回"已回绕"而不是错位数据；关掉日志开关时 reader 线程无 IO 调用（Process Monitor 可验）。

---

### D16 可测的解析器：spec → 字节 + 分段表

**出处**：`key_chars.c:22-115`

```c
// 输入："<ctrl+c>#delay:100<enter>" / "^C" / "\x1b[H"
// 输出：字节数组 + 每段的长度（用于在段之间插延时）
```

关键在**输出"分段表"而不是"字节 + 内联 sleep"**：

- 解析是**纯函数**，不需要通道、不需要线程 → 可以单测、可以 fuzz（`tests/test_key_chars.c` 就是这么做的）；
- 发送端拿着段表自己决定"发完这段等多久"，于是"延时"这件事只在一个地方实现（还顺便支持"整段一次性发出"以应对 `#delay:0`）；
- 支持 `#delay:N` 这种**语法内嵌时序**，让"按钮脚本"可以是一行字符串而不是代码。

**现状**：YTerminal 无键序列语法（只有"粘贴延时"，`paste_delay_ms`）；宏录制回放是固定时序（`features.c` 里的 `Sleep(80)`，B 部分已列）。另外 `src/keymap.c` 在工作区里是 tracked-deleted——旧版做过键位映射但没做成"可脚本化的序列"，这次用 spec 解析器正好把两件事合成一件。

**怎么实现**

1. `src/core/key_seq.h/.c`：

```c
typedef struct { unsigned char *bytes; int n; int *seg_off; int *seg_len; int seg_n; } YtKeySeq;
int yt_keyseq_parse(const char *spec, YtKeySeq *out, char *err, int cap);  // 纯函数
void yt_keyseq_free(YtKeySeq *);
```

2. 支持集与它一致：`<ctrl+x>`、`<esc>`、`<tab>`、`<enter>`、`<up/down/left/right>`、`<f1..f12>`、`^C` 写法、`\xHH`、`#delay:N`；**未知记号要报错并指出位置**（不要静默跳过）。
3. 单测 + fuzz：`test_keyseq.c` 覆盖 20 个用例（含畸形输入）；`fuzz_keyseq.c`（可选）随机串保证不崩、不越界。
4. UI 复用：快捷命令条、宏、按钮脚本、Agent 的 `/keys` 接口，**共用同一个解析器**。

**验收**：20 条用例全绿；畸形输入（`<ctrl+`、`\x`、超长串）返回错误且不崩；同一个 spec 在 UI 与 `/keys` 接口产生完全相同的字节（比对测试）。

---

## 7. 工程纪律与文档模板（D17）

**出处**：`docs/C-CODING-STANDARD.md`（10597 字节）、`DESIGN.md`（43487 字节）、`docs/运行时总框图.md`（16752 字节）、`tests/*`（24 个脚本）、`docs/Agent接口真板实测报告.md`（24889 字节）

### (1) 编码规范写成"可执行 + 可追溯"

它的规范不是"我们要注意内存安全"这种口号，而是三张表：

| 表 | 内容 | 迁移价值 |
|---|---|---|
| 强制规则表 | CERT C 的 10 条 + MISRA 子集 20 条，每条给"本项目落点" | 直接可用 |
| `system()` 白名单 | 只允许 3 个文件调用，且禁止拼接外部输入 | 本项目也要（`remotes.c` 起 `mstsc`/`vncviewer` 就是同类风险） |
| **偏离登记** | "MISRA 21.3 禁 malloc — 偏离，原因 X，补偿措施 Y" | 最值钱：**允许偏离，但必须留痕**，这样规则不会因为"做不到"而被整体废弃 |

**现状**：YTerminal 无编码规范文档；`build.ps1` 仅 `-Wall`；无静态分析、无 sanitizer 构建。

**怎么实现**

1. `docs/C-CODING-STANDARD.md`：照它的结构写（L1 语言标准 / L2 安全规则 / L3 行业标准 / L4 工具），然后**落到本项目实际**：Win32 句柄泄漏（`CreateFile` 后必 `CloseHandle`）、GDI 对象（`DeleteObject` 配对）、跨线程只用 `PostMessage`（不许直接碰控件）、JSON 解析禁手写 `strstr`（B14）。
2. 告警基线分两步：先 `-Wall -Wextra -Wno-unused-parameter` 且**零告警**，再加 `-Wformat=2 -Wshadow -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes`；`third_party/*` 单独 `-w`。
3. `clang --analyze`（mingw64 自带 clang 或另装）纳入 `quality.ps1`，基线"0 个真实缺陷"。
4. 偏离登记表放文档里，代码里用 `/* DEVIATION(MISRA-21.3): ... */` 注释，两者对得上。

### (2) 设计文档写成"可照着实现的蓝图"

`DESIGN.md` 的骨架值得整体借用（本项目 `docs/设计文档.md` 可以按这个重排）：

```
0. AI 重建总览（必须交付 / 架构原则 / 运行时总图 / 非目标）
1. 产品语义（一句话 / 通道类型 / 用户入口）
2. 模块与源码树
3. 数据面（Channel 抽象 / 各后端 / 时间戳 / 低延迟硬性禁令）
4. 控制面（线程模型 / Job 契约 / 目标选择 / 锁 / 门禁）
5. API 目录（按用途分节，每节给方法与路径）
6. 配置规格（查找顺序 / 顶层键 / 公共字段 / 热更新）
7-10. 各特性（按钮/共享入站/Tunnel/WebUI）
11-13. 发现与版本 / 端口约定 / 进程生命周期
14. 从零实现顺序（AI 按此执行）      ← 关键：顺序本身就是知识
15. 验收清单（发布前逐条勾）
16. 相对旧实现的架构差异（为何更优）
17. backlog（带 REQ 编号，防遗忘）
附录：最小配置 / Skill / 变更记录
```

第 14、16 两节是它独有的：**"从零实现顺序"**（先数据面后控制面，先 ring 后 job）把架构判断固化成步骤；**"相对旧实现为何更优"** 让后来者不会把已经踩过的坑再踩一遍。

### (3) 运行时图 + 测试资产命名

- `docs/运行时总框图.md`：先给**角色词典**（Board / 通道 / Agent / 宿主 …），再给 ASCII 总图，再写"读图（人/AI）"说明，最后给"选型决策树"。本项目照抄这个结构写"YTerminal 运行时总图"即可。
- `tests/` 的命名法本身就是分类：`bench_*`（性能基线）、`probe_*`（单点探测）、`diag_*`（一致性/延迟诊断）、`e2e_*`（端到端）、`stress_*`（压测）、`quality_check.py`（门禁）。**按前缀分类，比按功能起名更能让人知道该跑哪个**。
- `docs/Agent接口真板实测报告.md`（24.8KB）是"证据纪律"：不是"我测过了"，而是每条结论附真板型号、命令、输出片段、耗时数字。本项目发版前也该有一份这样的报告。

**验收**：`quality.ps1` 一条命令产出"告警数 / 单测结果 / 静态分析缺陷数"；`docs/` 里设计文档能被一个新人和一个 AI 各自照着走通；发版检查表 ≤ 20 条且每条可判定（是/否）。

---

## 8. 设计层不建议照搬（7 条）

| ssh-bridge 的做法 | 问题 | 本项目应改为 |
|---|---|---|
| `http_manager.c` 8400 行、`handle_get` 是几百行 `if (uri_eq(...))` 链 | 加一个接口要改 3 处、冲突不断；路由与帮助容易不同步 | **operations 表驱动**（D11），路由/帮助/测试同源 |
| `themes.css` 5936 行、约 150 套主题（含星座、DOS、π） | 维护债与认知负担；多数主题只服务"好玩" | 4-6 套令牌化主题；**只允许改令牌，不允许写组件特例** |
| `/lat` 未开启时返回 `200` + `{"error":"disabled"}`（`http_manager.c:6535-6554`） | 状态码说谎，客户端无法用状态码分支 | 未开启返回 `204` 或 `200 {"enabled":false, samples:0}`；错误一律用真状态码 |
| `GET /help` 正文里硬编码飞书 webhook 地址与 token（`:6484-6506`） | 凭据进源码、进二进制，谁拿到 exe 都能往群里灌消息 | 反馈地址走配置项；默认只记本地文件；**外部凭据不进仓库** |
| 口令比较是普通 `strcmp`（`util.c:528-533`），板卡口令明文进 JSON | 时序侧信道 + 配置文件裸奔 | 保持 DPAPI 存凭据（本项目已优于它）；比较用恒定时间；导出包改 PBKDF2（B23 已列） |
| 编译后 180 天禁用 API，进程仍启动（`main.c:269-272`） | 作为商业策略可以，作为开源默认会误伤 | **不引入**；但可学它的"优雅降级 + 结构化说明"（D6 的 `reply_feature_off`） |
| `ws_push_logs` 里 `mg_ws_send` 不检查 `c->send.len`（`:8336-8378`） | 慢客户端会让发送缓冲无界增长 | **加背压阈值 + 丢帧计数**（D9 第 4 点） |

---

## 9. 第二批实施顺序

| 阶段 | 内容 | 人日 | 出口标准 |
|---|---|---|---|
| 0（当天） | D1 + D2 + D6 + D14 + D16 | 4.5 | `Sleep(1)` 真 1ms；时间基准统一；响应带安全头；队列与解析器可单测 |
| 1 | D3 + D7 + D15 | 5 | 能读出"卡在哪一段"；单通道免参数；日志三条规则成立 |
| 2 | D4 + D5 | 5 | 有 CLI 排障入口；改配置不重建会话且有回执 |
| 3 | D8 + D9 + D10 | 9 | 长任务不堵 HTTP；WS 实时且不涨内存；主题/语言改数据不改代码 |
| 4 | D11 + D12 + D13 | 10 | `/api/help` 可执行；端口枚举正确；一键断电重启 + 内置 TFTP 真板跑通 |
| 5 | D17 | 3 | `quality.ps1` 一条命令出门禁报告；文档可被新人/AI 照做 |

**与第一部分的关系**（避免重复投入）：

- **D1/D2/D3 是 B4（overlapped 事件驱动）的前置观测手段**——先能量准，再动手改；否则改完无法证明变快。
- **D6/D7/D11 是 B15（Agent 接口 v2）的骨架**——B15 是"要哪些接口"，D11 是"接口怎么组织成契约"。
- **D9 是 B19（共享会话）与 B20（Web 控制台）的传输层**——先有 WS 通道，那两个才有地方挂。
- **D12 与 B9（终端态识别）组合**才产生"自动救板"这个真正的产品差异点；单独任一项都只是零件。
- **D14/D15/D16 是 B1/B2/B3/B8 的实现纪律**，不是新增功能。

---

## 10. 一句话结论（第二部分）

它真正稀缺的不是功能数量，而是**把"经验"写成"结构"的能力**：

1. 把"感觉卡"写成**可测量的槽位**（D1/D2/D3）；
2. 把"改配置"写成**可核对的差分**（D5）；
3. 把"接口能用"写成**一张自描述的表**（D6/D7/D11）；
4. 把"工程规范"写成**规则 + 偏离登记 + 门禁**（D17）；
5. 把"共享层"控制在**36 行**，其余各写各的（D14）。

这五条落进 YTerminal，比再抄十个功能更能决定它三年后还能不能维护。


