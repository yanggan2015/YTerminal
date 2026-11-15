$ErrorActionPreference = 'Continue'
$env:PATH = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:PATH
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root 'build'
$tp = Join-Path $root 'third_party\mingw64'
New-Item -ItemType Directory -Force -Path $build | Out-Null

$cflags = @(
  '-O2','-Wall','-DUNICODE','-D_UNICODE','-DUI_BRIDGE_ENABLE','-D_WIN32_WINNT=0x0A00',
  "-I$root\src\core","-I$root\src\ui","-I$root\bridge","-I$root\third_party","-I$tp\include"
)
$ld = @("-L$tp\lib",'-lssh2','-lssl','-lcrypto','-lz','-lws2_32','-lcrypt32','-luser32','-lgdi32','-lcomctl32','-lcomdlg32','-ladvapi32','-lshell32','-lole32')
$src = @(
  "$root\src\ui\main.c",
  "$root\src\ui\app_state.c",
  "$root\src\ui\app_tree.c",
  "$root\src\ui\app_connect.c",
  "$root\src\ui\term_view.c",
  "$root\src\ui\ui_extra.c",
  "$root\src\core\paths.c",
  "$root\src\core\cred_store.c",
  "$root\src\core\profile_store.c",
  "$root\src\core\session.c",
  "$root\src\core\window_store.c",
  "$root\src\core\features.c",
  "$root\src\engine\ssh_session.c",
  "$root\src\engine\ssh_async.c",
  "$root\src\engine\local_pty.c",
  "$root\src\engine\serial_session.c",
  "$root\src\engine\telnet_session.c",
  "$root\src\engine\sftp.c",
  "$root\src\engine\forward.c",
  "$root\src\engine\zmodem.c",
  "$root\src\engine\remotes.c",
  "$root\bridge\win_ui_bridge.c",
  "$root\third_party\mongoose.c",
  "$root\third_party\cJSON.c"
)

Write-Host 'Building YTerminal (Test+Bridge)...'
gcc @cflags -o "$build\YTerminal.exe" @src -mwindows -municode '-Wl,--stack,8388608' @ld
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Copy-Item "$tp\bin\*.dll" $build -Force
Write-Host "OK: $build\YTerminal.exe"
Get-Item "$build\YTerminal.exe" | Format-Table Name,Length,LastWriteTime
