# 无侵入式界面截图：用 PrintWindow 抓取被遮挡的 YTerminal 窗口
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts\ui-shot.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\ui-shot.ps1 -SessionDialog
# 说明：不移动鼠标、不抢焦点；DPI 150% 下按缩放倍率生成位图，避免右侧被裁。

param(
    [switch]$SessionDialog,
    [string]$OutDir = "build"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class YShot {
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public static List<IntPtr> ByPid(uint pid) {
        var l = new List<IntPtr>();
        EnumWindows((h, p) => { uint q; GetWindowThreadProcessId(h, out q); if (q == pid && IsWindowVisible(h)) l.Add(h); return true; }, IntPtr.Zero);
        return l;
    }
    public static string Save(IntPtr h, string path) {
        RECT r; GetWindowRect(h, out r);
        double sc = GetDpiForWindow(h) / 96.0;
        int w = (int)((r.Right - r.Left) * sc), hg = (int)((r.Bottom - r.Top) * sc);
        var bmp = new System.Drawing.Bitmap(w, hg);
        var g = System.Drawing.Graphics.FromImage(bmp);
        var dc = g.GetHdc();
        PrintWindow(h, dc, 2);
        g.ReleaseHdc(dc);
        bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
        g.Dispose(); bmp.Dispose();
        return w + "x" + hg;
    }
}
'@

$exe = Join-Path $PSScriptRoot "..\build\YTerminal.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "未找到 $exe，请先运行 build.ps1" }
$resolvedOut = Join-Path (Resolve-Path $PSScriptRoot).Path $OutDir
if (-not (Test-Path -LiteralPath $resolvedOut)) { New-Item -ItemType Directory -Path $resolvedOut | Out-Null }

$proc = Get-Process -Name YTerminal -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) {
    Start-Process -FilePath $exe
    Start-Sleep -Seconds 3
    $proc = Get-Process -Name YTerminal | Select-Object -First 1
}

$main = [YShot]::Save($proc.MainWindowHandle, (Join-Path $resolvedOut "ui_check.png"))
Write-Output "main window: $main"

if ($SessionDialog) {
    [YShot]::PostMessageW($proc.MainWindowHandle, 0x0111, [IntPtr]1001, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds 1200
    $dlg = $null
    foreach ($h in [YShot]::ByPid([uint32]$proc.Id)) {
        $r = New-Object YShot+RECT
        [YShot]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.Right - $r.Left
        if ($w -gt 500 -and $w -lt 700) { $dlg = $h }
    }
    if ($dlg) {
        $sz = [YShot]::Save($dlg, (Join-Path $resolvedOut "ui_session.png"))
        Write-Output "session dialog: $sz"
        [YShot]::PostMessageW($dlg, 0x0010, [IntPtr]0, [IntPtr]0) | Out-Null
    } else {
        Write-Output "no session dialog found"
    }
}
