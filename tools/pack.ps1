$ErrorActionPreference = 'Continue'
$env:PATH = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:PATH
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$dist = Join-Path $root 'dist\YTerminal'
$tp = Join-Path $root 'third_party\mingw64'

# Release build (no UI bridge)
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $dist, "$dist\config", "$dist\logs", "$dist\themes", "$dist\keys",
  "$dist\runtime\x11" | Out-Null

$cflags = @(
  '-O2','-Wall','-DUNICODE','-D_UNICODE','-D_WIN32_WINNT=0x0A00',
  "-I$root\src\core","-I$root\src\ui","-I$root\bridge","-I$root\third_party","-I$tp\include"
)
$ld = @("-L$tp\lib",'-lssh2','-lssl','-lcrypto','-lz','-lws2_32','-lcrypt32','-luser32','-lgdi32','-lcomctl32','-lcomdlg32','-ladvapi32','-lshell32','-lole32')
$src = @(
  "$root\src\ui\main.c","$root\src\ui\app_state.c","$root\src\ui\app_tree.c","$root\src\ui\app_connect.c",
  "$root\src\ui\term_view.c","$root\src\ui\ui_extra.c",
  "$root\src\core\paths.c","$root\src\core\cred_store.c","$root\src\core\profile_store.c","$root\src\core\session.c",
  "$root\src\core\window_store.c","$root\src\core\features.c",
  "$root\src\engine\ssh_session.c","$root\src\engine\ssh_async.c","$root\src\engine\local_pty.c","$root\src\engine\serial_session.c",
  "$root\src\engine\telnet_session.c",
  "$root\src\engine\sftp.c","$root\src\engine\forward.c","$root\src\engine\zmodem.c","$root\src\engine\remotes.c"
)

Write-Host 'Release build (no bridge)...'
gcc @cflags -o "$dist\YTerminal.exe" @src -mwindows -municode '-Wl,--stack,8388608' @ld
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Copy-Item "$tp\bin\libssh2-1.dll","$tp\bin\libssl-3-x64.dll","$tp\bin\libcrypto-3-x64.dll","$tp\bin\zlib1.dll" $dist -Force
Copy-Item "$root\themes\*" "$dist\themes\" -Force -ErrorAction SilentlyContinue
Copy-Item "$root\runtime\*" "$dist\runtime\" -Recurse -Force -ErrorAction SilentlyContinue
@"
YTerminal Portable
==================
1. 解压本目录到任意路径，双击 YTerminal.exe
2. 无需安装 OpenSSH / VC++ 运行库（已旁载 libssh2/openssl）
3. 可选旁载：runtime\x11\vcxsrv.exe
4. 协议：SSH / SFTP / 串口 / 本地 / Telnet
5. 配置/会话/日志均在本目录 config\ logs\ keys\
"@ | Set-Content -Encoding UTF8 "$dist\README.txt"

Write-Host "Packed: $dist"
Get-ChildItem $dist | Format-Table Name,Length
