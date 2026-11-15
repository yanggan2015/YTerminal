#ifndef YT_AGENT_H
#define YT_AGENT_H
/* AI/Agent 控制面：自描述 help + 通道读写 + 锁租约（借鉴 ssh-bridge B15） */
#include "yt_core.h"

typedef struct {
    int (*ntabs)(void);
    YtSession *(*tab_at)(int i);
} YtAgentHooks;

void yt_agent_set_hooks(const YtAgentHooks *h);
/* 处理 Agent 路由；命中返回 1 并写入 *status 与 *json（调用方 free）；未命中返回 0 */
int  yt_agent_try(const char *method, const char *uri, const char *query,
                  const char *body, int *status, char **json, int *is_text);

#endif
