# YTerminal 界面冒烟测试：启动真实 exe，逐窗验证控件存在与列表数据
# 用法：powershell -ExecutionPolicy Bypass -File scripts\ui-smoke.ps1
# 退出码 0 = 全部通过；1 = 有失败项

$ErrorActionPreference = "Stop"
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class YSmoke {
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public static int Width(IntPtr h) { RECT r; GetWindowRect(h, out r); return r.Right - r.Left; }
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)] public static extern int GetWindowTextW(IntPtr h, StringBuilder sb, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, StringBuilder l);
    public delegate bool EnumProc(IntPtr h, IntPtr l);

    public static List<IntPtr> Top(uint pid) {
        var l = new List<IntPtr>();
        EnumWindows((h, x) => { uint q; GetWindowThreadProcessId(h, out q); if (q == pid && IsWindowVisible(h)) l.Add(h); return true; }, IntPtr.Zero);
        return l;
    }
    public static string Cls(IntPtr h) { var sb = new StringBuilder(160); GetClassNameW(h, sb, 160); return sb.ToString(); }
    public static string Title(IntPtr h) { var sb = new StringBuilder(160); GetWindowTextW(h, sb, 160); return sb.ToString(); }
    public static IntPtr ByClass(uint pid, string cls) {
        IntPtr f = IntPtr.Zero;
        foreach (var h in Top(pid)) if (Cls(h) == cls) f = h;
        return f;
    }
    public static List<IntPtr> Kids(IntPtr p) { var l = new List<IntPtr>(); EnumChildWindows(p, (h, x) => { l.Add(h); return true; }, IntPtr.Zero); return l; }
    public static IntPtr ById(IntPtr p, int id) {
        foreach (var h in Kids(p)) if (GetDlgCtrlID(h) == id) return h;
        return IntPtr.Zero;
    }
    public static int Count(IntPtr lb) { return (int)SendMessageW(lb, 0x018B, IntPtr.Zero, IntPtr.Zero); }
    public static string Item(IntPtr lb, int i) { var sb = new StringBuilder(512); SendMessageW(lb, 0x0189, (IntPtr)i, sb); return sb.ToString(); }
}
'@

$exe = Join-Path $PSScriptRoot "..\build\YTerminal.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "未找到 $exe，请先运行 build.ps1" }

$env:YT_UI_SELFTEST = "1"
$fails = 0
function Check([string]$name, [bool]$ok, [string]$detail) {
    if ($ok) { Write-Output ("PASS  " + $name + "  " + $detail) }
    else { Write-Output ("FAIL  " + $name + "  " + $detail); $script:fails++ }
}

Get-Process -Name YTerminal -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Process -FilePath $exe | Out-Null
Start-Sleep -Seconds 3
$proc = Get-Process -Name YTerminal | Select-Object -First 1
$main = $proc.MainWindowHandle
Check "主窗口" ($main -ne 0) ("hwnd=" + $main)
Check "主窗口类名" ([YSmoke]::Cls($main) -eq "YTerminalMain") ([YSmoke]::Cls($main))
$kids = [YSmoke]::Kids($main)
Check "主窗口子控件" ($kids.Count -ge 6) ("子控件 " + $kids.Count + " 个：" + (($kids | ForEach-Object { [YSmoke]::Cls($_) }) -join ","))

# 命令面板 / 快捷键 / 端口转发 / 设置：验证弹出窗口并截图留档
foreach ($c in @(@(1050,"ui_palette.png"),@(1041,"ui_shortcuts.png"),@(1054,"ui_tunnels.png"),@(1034,"ui_settings.png"),@(1045,"ui_keymap.png"))) {
    $id = $c[0]
    [YSmoke]::PostMessageW($main, 0x0111, [IntPtr]$id, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds 1500
    $dlg = [IntPtr]::Zero
    foreach ($h in [YSmoke]::Top([uint32]$proc.Id)) {
        if ($h -eq $main) { continue }
        $cls = [YSmoke]::Cls($h)
        if ($cls -match "BAIDU|IME|ToolTip|GDI") { continue }
        if ([YSmoke]::Width($h) -lt 500) { continue }
        $dlg = $h
    }
    Check ("命令 " + $id + " 弹窗") ($dlg -ne [IntPtr]::Zero) ("标题=" + [YSmoke]::Title($dlg) + " 类=" + [YSmoke]::Cls($dlg))
    if ($dlg -ne [IntPtr]::Zero) { [YSmoke]::PostMessageW($dlg, 0x0010, [IntPtr]0, [IntPtr]0) | Out-Null }
    Start-Sleep -Milliseconds 400
}

# 快捷键设置：类名、条目数、首项文案、录制/保存按钮
[YSmoke]::PostMessageW($main, 0x0111, [IntPtr]1045, [IntPtr]0) | Out-Null
Start-Sleep -Seconds 1
$km = [YSmoke]::ByClass([uint32]$proc.Id, "YKmDlg")
Check "快捷键设置窗口" ($km -ne [IntPtr]::Zero) ("hwnd=" + $km)
if ($km -ne [IntPtr]::Zero) {
    $kl = [YSmoke]::ById($km, 4201)
    Check "快捷键列表存在" ($kl -ne [IntPtr]::Zero) ""
    $kn = [YSmoke]::Count($kl)
    Check "快捷键条目 >= 16" ($kn -ge 16) ("条数=" + $kn)
    $k0 = if ($kn -gt 0) { [YSmoke]::Item($kl, 0) } else { "" }
    Check "首项是复制选区" ($k0 -match "复制选区") ("首项=[" + $k0 + "]")
    Check "修改/保存按钮存在" (([YSmoke]::ById($km, 4203) -ne [IntPtr]::Zero) -and ([YSmoke]::ById($km, 4205) -ne [IntPtr]::Zero)) ""
    [YSmoke]::PostMessageW($km, 0x0010, [IntPtr]0, [IntPtr]0) | Out-Null
}
# SFTP 双栏：验证窗口、两个列表、本地列表内容（可捕捉 UNICODE 宏错配）
[YSmoke]::PostMessageW($main, 0x0111, [IntPtr]1033, [IntPtr]0) | Out-Null
Start-Sleep -Seconds 2
$sf = [YSmoke]::ByClass([uint32]$proc.Id, "YSftpDual")
Check "SFTP 窗口" ($sf -ne [IntPtr]::Zero) ("hwnd=" + $sf)
if ($sf -ne [IntPtr]::Zero) {
    $l = [YSmoke]::ById($sf, 4103)
    $r = [YSmoke]::ById($sf, 4104)
    Check "SFTP 左右列表" (($l -ne [IntPtr]::Zero) -and ($r -ne [IntPtr]::Zero)) ""
    $n = [YSmoke]::Count($l)
    Check "SFTP 本地列表有内容" ($n -ge 2) ("条数=" + $n)
    $first = if ($n -gt 0) { [YSmoke]::Item($l, 0) } else { "" }
    $second = if ($n -gt 1) { [YSmoke]::Item($l, 1) } else { "" }
    Check "SFTP 列表首项是上级目录" ($first -eq "..") ("首项=[" + $first + "]")
    Check "SFTP 文件名不是单字符" ($second.Length -gt 1) ("第二项=[" + $second + "] 长度=" + $second.Length)
    [YSmoke]::PostMessageW($sf, 0x0010, [IntPtr]0, [IntPtr]0) | Out-Null
}

Start-Sleep -Milliseconds 500
Get-Process -Name YTerminal -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Output ("结果：" + $(if ($fails -eq 0) { "全部通过" } else { "$fails 项失败" }))
exit $(if ($fails -eq 0) { 0 } else { 1 })
