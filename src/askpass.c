/* YTPass - SSH_ASKPASS 辅助程序：把环境变量 YT_ASKPASS 的内容打印到 stdout */
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

int wmain(void) {
    const wchar_t *p = _wgetenv(L"YT_ASKPASS");
    if (!p) {
        wchar_t buf[512] = { 0 };
        if (GetEnvironmentVariableW(L"YT_ASKPASS", buf, 512) > 0) p = buf;
    }
    if (p) wprintf(L"%ls\n", p);
    return 0;
}
