$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root
$env:PATH = "C:\msys64\mingw64\bin;" + $env:PATH
$build = Join-Path $root 'build'
if (-not (Test-Path -LiteralPath $build)) { New-Item -ItemType Directory -Path $build | Out-Null }

$cc = 'gcc'
$cflags = @('-c','-O2','-Wall','-Wextra','-Wno-unused-parameter','-Wno-cast-function-type','-Wno-stringop-truncation','-D_WIN32_WINNT=0x0A00','-DWIN32_LEAN_AND_MEAN')
$libs = @('-lcomctl32','-lcomdlg32','-lgdi32','-luser32','-ladvapi32','-lole32','-loleaut32','-luuid','-lshell32','-lshlwapi','-lws2_32','-ldwmapi','-limm32','-lmsimg32','-lcrypt32','-lwinmm','-lshlwapi')

$srcs = Get-ChildItem -LiteralPath (Join-Path $root 'src') -Filter '*.c' |
        Where-Object { $_.Name -ne 'askpass.c' } | Sort-Object Name
$objs = @()
$failed = 0
foreach ($s in $srcs) {
    $o = Join-Path $build ($s.BaseName + '.o')
    $objs += $o
    & $cc @cflags (Join-Path 'src' $s.Name) -o $o
    if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: $($s.Name)"; $failed = 1 }
}
if ($failed) { exit 1 }

# 资源（清单 + 版本信息）
& windres 'src\app.rc' -O coff -o (Join-Path $build 'app.res.o')
if ($LASTEXITCODE -ne 0) { exit 1 }
$objs += (Join-Path $build 'app.res.o')

& $cc -o (Join-Path $build 'YTerminal.exe') @objs @libs '-municode' '-mwindows' '-static' '-static-libgcc' '-s'
if ($LASTEXITCODE -ne 0) { exit 1 }

# SSH_ASKPASS 辅助程序
& $cc '-O2' '-DWIN32_LEAN_AND_MEAN' 'src\askpass.c' -o (Join-Path $build 'YTPass.exe') '-municode' '-static' '-s'
if ($LASTEXITCODE -ne 0) { exit 1 }

$exe = Join-Path $build 'YTerminal.exe'
$size = (Get-Item -LiteralPath $exe).Length
Write-Host ("BUILD OK  {0}  ({1:N0} KB)" -f $exe, ($size/1KB))
