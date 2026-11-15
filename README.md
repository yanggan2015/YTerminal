# YTerminal

绿色便携 Windows 全能终端（SSH / SFTP / 串口 / 本地 ConPTY / Telnet；可选 X11 旁载）。

## 构建

```powershell
.\build.ps1          # Test + UI Bridge :8086
.\tools\pack.ps1     # Release → dist\YTerminal\
```

依赖：MSYS2 MinGW64 `gcc`；第三方库已 vendor 于 `third_party/mingw64`。

## 运行

- 开发：`build\YTerminal.exe`
- 绿色包：`dist\YTerminal\YTerminal.exe`（整夹拷贝即可）

## 文档

见 `docs/`：设计 / 测试 / 评分 / 终端调研 / 合并功能列表。

## 许可

以仓库内 LICENSE 为准（若缺失则按源码版权声明）。
