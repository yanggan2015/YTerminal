# 旧 Makefile 源集已废弃。请用 PowerShell 构建当前产品：
#   powershell -File build.ps1
# 发布包：
#   powershell -File tools/pack.ps1

.PHONY: all release clean
all release:
	@echo "Use: powershell -File build.ps1" && exit 1
clean:
	rm -rf build
