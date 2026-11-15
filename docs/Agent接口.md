# YTerminal Agent 接口（AI 控制面）

借鉴 ssh-bridge：自描述 `/api/help`、通道清单、输出游标、键序列、标记执行、锁租约。仅监听 `127.0.0.1:8086`，需 `X-Ui-Token`（见 `config/ui_bridge.token`）。

## 快速开始

```bash
TOKEN=$(cat config/ui_bridge.token)
curl -H "X-Ui-Token: $TOKEN" http://127.0.0.1:8086/api/help
curl -H "X-Ui-Token: $TOKEN" http://127.0.0.1:8086/channels
curl -H "X-Ui-Token: $TOKEN" "http://127.0.0.1:8086/channels/0/log?since=0"
curl -H "X-Ui-Token: $TOKEN" -d "{\"text\":\"echo hi\\r\"}" http://127.0.0.1:8086/channels/0/write
curl -H "X-Ui-Token: $TOKEN" -d "{\"spec\":\"uname -a<enter>\"}" http://127.0.0.1:8086/channels/0/keys
curl -H "X-Ui-Token: $TOKEN" -d "{\"cmd\":\"pwd\",\"timeout_ms\":5000}" http://127.0.0.1:8086/channels/0/exec
curl -H "X-Ui-Token: $TOKEN" -d "{\"session\":\"bot1\",\"ttl_ms\":60000}" http://127.0.0.1:8086/lock
```

Skill / Agent：**先读 `/api/help`，再按 operations 调用**，不要写死路径。

## 注意

- `/exec` 仅 Local/SSH：先发命令，安静后再发 `echo MARK`，避免回显假成功。
- `#delay:N` 解析已修复；单实例持有 8086，第二窗口不覆盖 `ui_bridge.token`。
