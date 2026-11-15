# YTerminal v1.2.0

纯 C（无 Python、无运行时）的 Windows 全能终端，单 EXE 275 KB。

## 本版重点：把未满分项补上

### 1. SFTP 双栏管理器（重写）

对标 MobaXterm / WindTerm 的文件页：左本地、右远端，一次看两侧。

- 双栏并列 + 分区标题（本地文件 / 远端文件）+ 中央分隔线
- 本地 `FindFirstFileW` 扫描、目录在前；双击目录进入，`..` 回上级
- 远端 `ssh "cd '<cwd>' && pwd && ls -1Ap"`，目录以 `/` 结尾，双击进入
- 多选（Ctrl/Shift）后「上传 →」「← 下载」批量入队，`scp` 后台线程顺序执行
- 跑马灯进度条 + 「正在传输 i/n」状态；传输中禁止关窗
- 远端目录读取也在后台线程，网络慢不会把窗口卡成「未响应」
- 本地/远端列表均加横向滚动，长文件名可读
- 路径栏可直接编辑回车跳转；刷新一键同步两侧

### 2. 分屏比例记忆

拖动左右/上下分割线松手即写入 `settings.json`（`split_v_pct` / `split_h_pct`），下次启动沿用；非法值回落 50%。

### 3. 工程级修复：UNICODE 构建宏

此前构建缺少 `-DUNICODE/-D_UNICODE`，`ListBox_AddString` 走 ANSI 版本，Unicode 列表框把宽字符串按 ANSI 读取——列表每行只剩一个字符。现已修复，并统一宽字符 printf 的 `%ls/%hs` 写法。

### 4. 界面冒烟测试

`scripts/ui-smoke.ps1`：启动真实 exe，逐个打开命令面板 / 快捷键 / 端口转发 / 设置 / SFTP，断言窗口类名、控件存在、本地列表条数与首项，退出码 0/1。可直接接 CI（见 `.github/workflows/ui-smoke.yml`，手动触发）。

### 5. 新文档

[`docs/工具独特亮点.md`](docs/工具独特亮点.md)：逐个拆解 12 个主流终端/串口工具「为什么有人用」，并给出 YTerminal 的落点与状态（✅/◐/○）。

## 验证

```
build.ps1            BUILD OK  275 KB  零告警
test_vt.exe          46 passed
test_core.exe        27 passed
ui-smoke.ps1         全部通过（6 窗口 + SFTP 列表 57 项）
```

## 已知限制

- SFTP 传输走系统 `scp`，暂不支持目录递归与断点续传
- 快捷键仍不可自定义；无命令历史补全；无 Zmodem；无内置 SSH 栈
- 整体多维评分 8.4/10，未满分项清单见 `docs/评分表.md` §4
