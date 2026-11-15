#include "yt_agent.h"
#include "log_ring.h"
#include "key_chars.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

static YtAgentHooks g_hooks;
static DWORD g_start_tick;
static char g_lock_sess[64];
static DWORD g_lock_until;
static CRITICAL_SECTION g_lock_cs;
static LONG g_lock_inited;

static void lock_init(void)
{
    if (InterlockedCompareExchange(&g_lock_inited, 1, 0) == 0)
        InitializeCriticalSection(&g_lock_cs);
}

void yt_agent_set_hooks(const YtAgentHooks *h)
{
    if (h) g_hooks = *h;
    if (!g_start_tick) g_start_tick = GetTickCount();
    lock_init();
}

static int uri_eq(const char *uri, const char *s)
{
    return uri && s && strcmp(uri, s) == 0;
}

static int parse_ch(const char *uri, char *rest, int rcap)
{
    /* /channels/N/... → N, rest=suffix */
    int id = -1;
    const char *p;
    if (!uri || strncmp(uri, "/channels/", 10) != 0) return -1;
    p = uri + 10;
    if (*p < '0' || *p > '9') return -1;
    id = atoi(p);
    while (*p >= '0' && *p <= '9') p++;
    if (rest && rcap > 0) {
        strncpy(rest, p, (size_t)rcap - 1);
        rest[rcap - 1] = 0;
    }
    return id;
}

static YtSession *sess_at(int id)
{
    if (!g_hooks.tab_at || !g_hooks.ntabs) return NULL;
    if (id < 0 || id >= g_hooks.ntabs()) return NULL;
    return g_hooks.tab_at(id);
}

static int lock_held_ok(const char *agent_sess)
{
    DWORD now = GetTickCount();
    EnterCriticalSection(&g_lock_cs);
    if (g_lock_sess[0] && (int)(now - g_lock_until) < 0) {
        int ok = agent_sess && strcmp(agent_sess, g_lock_sess) == 0;
        LeaveCriticalSection(&g_lock_cs);
        return ok;
    }
    g_lock_sess[0] = 0;
    LeaveCriticalSection(&g_lock_cs);
    return 1; /* 无锁 */
}

static const char *hdr_sess(const char *body)
{
    /* body 可选 {"session":"..."}；也支持 query 由调用方拼进 body 前缀 — 简化：JSON session */
    static char s[64];
    cJSON *j, *o;
    s[0] = 0;
    if (!body || !body[0]) return s;
    j = cJSON_Parse(body);
    if (!j) return s;
    o = cJSON_GetObjectItem(j, "session");
    if (cJSON_IsString(o) && o->valuestring)
        strncpy(s, o->valuestring, sizeof(s) - 1);
    cJSON_Delete(j);
    return s;
}

static char *json_dup(cJSON *o)
{
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return s;
}

static char *help_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *ops = cJSON_AddArrayToObject(root, "operations");
    int n = g_hooks.ntabs ? g_hooks.ntabs() : 0;
    cJSON_AddStringToObject(root, "tool", "YTerminal");
    cJSON_AddStringToObject(root, "version", "1.0.0");
    cJSON_AddNumberToObject(root, "channels", n);
    cJSON_AddStringToObject(root, "note", "Agent first: GET /api/help then call operations. Token: X-Ui-Token or ?token=");
#define OP(m,p,sum) do { cJSON *o=cJSON_CreateObject(); \
    cJSON_AddStringToObject(o,"method",m); cJSON_AddStringToObject(o,"path",p); \
    cJSON_AddStringToObject(o,"summary",sum); cJSON_AddItemToArray(ops,o); } while(0)
    OP("GET", "/health", "liveness + uptime + channel count");
    OP("GET", "/api/help", "machine-readable operations");
    OP("GET", "/help", "human markdown");
    OP("GET", "/channels", "list open tabs as channels");
    OP("GET", "/channels/{id}/log?since=", "incremental terminal bytes via ring");
    OP("POST", "/channels/{id}/write", "body {\"text\":\"...\"} raw write");
    OP("POST", "/channels/{id}/keys", "body {\"spec\":\"ls<enter>\"} key DSL");
    OP("POST", "/channels/{id}/exec", "body {\"cmd\":\"uname -a\",\"timeout_ms\":8000} marker wait");
    OP("POST", "/lock", "body {\"session\":\"ai-1\",\"ttl_ms\":60000}");
    OP("POST", "/unlock", "body {\"session\":\"ai-1\"}");
#undef OP
    if (n > 0) {
        char ex[128];
        snprintf(ex, sizeof(ex), "curl -H \"X-Ui-Token: TOKEN\" \"http://127.0.0.1:8086/channels/0/log?since=0\"");
        cJSON_AddStringToObject(root, "example", ex);
    }
    return json_dup(root);
}

static char *help_md(void)
{
    static char buf[2048];
    int n = g_hooks.ntabs ? g_hooks.ntabs() : 0;
    snprintf(buf, sizeof(buf),
        "# YTerminal Agent\n\n"
        "1. `GET /health`\n"
        "2. `GET /api/help` — operations JSON\n"
        "3. `GET /channels` — open tabs (%d)\n"
        "4. `GET /channels/0/log?since=0` — read output\n"
        "5. `POST /channels/0/write` `{\"text\":\"echo hi\\r\"}`\n"
        "6. `POST /channels/0/keys` `{\"spec\":\"uname -a<enter>\"}`\n"
        "7. `POST /channels/0/exec` `{\"cmd\":\"pwd\",\"timeout_ms\":5000}`\n"
        "8. `POST /lock` / `POST /unlock` with `{\"session\":\"...\",\"ttl_ms\":60000}`\n\n"
        "Auth: header `X-Ui-Token` (file config/ui_bridge.token).\n",
        n);
    return _strdup(buf);
}

static int do_exec(YtSession *s, const char *cmd, int timeout_ms, char **out_json)
{
    char mark[64], line[1100], chunk[8192];
    unsigned long long since;
    DWORD t0;
    cJSON *root;
    int timed = 0, got = 0;
    size_t clen;
    if (!s || !cmd || !cmd[0]) return 400;
    if (timeout_ms < 200) timeout_ms = 200;
    if (timeout_ms > 120000) timeout_ms = 120000;
    snprintf(mark, sizeof(mark), "YT_END_%08lx", GetTickCount() ^ GetCurrentThreadId());
    clen = strlen(cmd);
    while (clen && (cmd[clen - 1] == '\r' || cmd[clen - 1] == '\n')) clen--;
    snprintf(line, sizeof(line), "%.*s; echo %s\r", (int)clen, cmd, mark);
    since = s->ring ? yt_ring_cursor((YtRing *)s->ring) : 0;
    yt_session_write(s, line, (int)strlen(line));
    t0 = GetTickCount();
    chunk[0] = 0;
    while ((int)(GetTickCount() - t0) < timeout_ms) {
        char piece[4096];
        unsigned long long ns = since;
        int m;
        if (s->ring) {
            m = yt_ring_read_since((YtRing *)s->ring, since, piece, (int)sizeof(piece), &ns);
            if (m > 0) {
                size_t have = strlen(chunk);
                if (have + (size_t)m < sizeof(chunk) - 1) {
                    memcpy(chunk + have, piece, (size_t)m);
                    chunk[have + (size_t)m] = 0;
                }
                since = ns;
                if (strstr(chunk, mark)) { got = 1; break; }
            }
        }
        Sleep(20);
    }
    if (!got) timed = 1;
    root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", got ? 1 : 0);
    cJSON_AddBoolToObject(root, "timed_out", timed);
    cJSON_AddStringToObject(root, "marker", mark);
    cJSON_AddStringToObject(root, "output", chunk);
    cJSON_AddNumberToObject(root, "since", (double)since);
    *out_json = json_dup(root);
    return got ? 200 : 504;
}

int yt_agent_try(const char *method, const char *uri, const char *query,
                 const char *body, int *status, char **json, int *is_text)
{
    char rest[64];
    int cid;
    (void)query;
    if (!status || !json || !is_text) return 0;
    *is_text = 0;
    *json = NULL;
    lock_init();

    if (uri_eq(uri, "/health") && (!method || !strcmp(method, "GET"))) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", 1);
        cJSON_AddStringToObject(o, "version", "1.0.0");
        cJSON_AddNumberToObject(o, "uptime_s",
            (double)((GetTickCount() - g_start_tick) / 1000));
        cJSON_AddNumberToObject(o, "channels", g_hooks.ntabs ? g_hooks.ntabs() : 0);
        *status = 200; *json = json_dup(o); return 1;
    }
    if (uri_eq(uri, "/api/help") && (!method || !strcmp(method, "GET"))) {
        *status = 200; *json = help_json(); return 1;
    }
    if (uri_eq(uri, "/help") && (!method || !strcmp(method, "GET"))) {
        *status = 200; *json = help_md(); *is_text = 1; return 1;
    }
    if (uri_eq(uri, "/channels") && (!method || !strcmp(method, "GET"))) {
        cJSON *o = cJSON_CreateObject(), *arr = cJSON_AddArrayToObject(o, "channels");
        int i, n = g_hooks.ntabs ? g_hooks.ntabs() : 0;
        for (i = 0; i < n; i++) {
            YtSession *s = sess_at(i);
            cJSON *c = cJSON_CreateObject();
            char title[128] = "";
            cJSON_AddNumberToObject(c, "id", i);
            cJSON_AddNumberToObject(c, "kind", s ? (int)s->kind : -1);
            cJSON_AddNumberToObject(c, "state", s ? (int)s->state : -1);
            if (s) WideCharToMultiByte(CP_UTF8, 0, s->title, -1, title, sizeof(title), NULL, NULL);
            cJSON_AddStringToObject(c, "title", title);
            cJSON_AddNumberToObject(c, "cursor",
                s && s->ring ? (double)yt_ring_cursor((YtRing *)s->ring) : 0);
            cJSON_AddItemToArray(arr, c);
        }
        *status = 200; *json = json_dup(o); return 1;
    }
    if (uri_eq(uri, "/lock") && method && !strcmp(method, "POST")) {
        cJSON *j = cJSON_Parse(body ? body : "{}");
        const char *sess = NULL; int ttl = 60000;
        cJSON *o, *resp;
        if (j) {
            o = cJSON_GetObjectItem(j, "session");
            if (cJSON_IsString(o)) sess = o->valuestring;
            o = cJSON_GetObjectItem(j, "ttl_ms");
            if (cJSON_IsNumber(o)) ttl = (int)o->valuedouble;
        }
        if (!sess || !sess[0]) { cJSON_Delete(j); *status = 400; *json = _strdup("{\"ok\":false,\"msg\":\"session required\"}"); return 1; }
        if (ttl < 1000) ttl = 1000; if (ttl > 3600000) ttl = 3600000;
        EnterCriticalSection(&g_lock_cs);
        if (g_lock_sess[0] && (int)(GetTickCount() - g_lock_until) < 0 && strcmp(g_lock_sess, sess) != 0) {
            LeaveCriticalSection(&g_lock_cs);
            cJSON_Delete(j);
            *status = 409; *json = _strdup("{\"ok\":false,\"msg\":\"locked by other session\"}"); return 1;
        }
        strncpy(g_lock_sess, sess, sizeof(g_lock_sess) - 1);
        g_lock_until = GetTickCount() + (DWORD)ttl;
        LeaveCriticalSection(&g_lock_cs);
        cJSON_Delete(j);
        resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "ok", 1);
        cJSON_AddStringToObject(resp, "session", g_lock_sess);
        cJSON_AddNumberToObject(resp, "ttl_ms", ttl);
        *status = 200; *json = json_dup(resp); return 1;
    }
    if (uri_eq(uri, "/unlock") && method && !strcmp(method, "POST")) {
        const char *sess = hdr_sess(body);
        EnterCriticalSection(&g_lock_cs);
        if (!g_lock_sess[0] || (sess && strcmp(sess, g_lock_sess) == 0))
            g_lock_sess[0] = 0;
        else {
            LeaveCriticalSection(&g_lock_cs);
            *status = 403; *json = _strdup("{\"ok\":false,\"msg\":\"session mismatch\"}"); return 1;
        }
        LeaveCriticalSection(&g_lock_cs);
        *status = 200; *json = _strdup("{\"ok\":true}"); return 1;
    }

    cid = parse_ch(uri, rest, (int)sizeof(rest));
    if (cid >= 0) {
        YtSession *s = sess_at(cid);
        if (!s) { *status = 404; *json = _strdup("{\"ok\":false,\"msg\":\"channel not found\"}"); return 1; }
        if (!lock_held_ok(hdr_sess(body)) && method && strcmp(method, "GET") != 0) {
            *status = 423; *json = _strdup("{\"ok\":false,\"msg\":\"held by other agent lock\"}"); return 1;
        }
        if (!strcmp(rest, "/log") && (!method || !strcmp(method, "GET"))) {
            char qsince[32] = "0", out[16384];
            unsigned long long since = 0, ns = 0;
            int m; cJSON *o;
            if (query) {
                /* since= from query string manually */
                const char *ps = strstr(query, "since=");
                if (ps) since = _strtoui64(ps + 6, NULL, 10);
            }
            (void)qsince;
            m = s->ring ? yt_ring_read_since((YtRing *)s->ring, since, out, (int)sizeof(out), &ns) : 0;
            o = cJSON_CreateObject();
            cJSON_AddBoolToObject(o, "ok", 1);
            cJSON_AddNumberToObject(o, "id", cid);
            cJSON_AddNumberToObject(o, "since", (double)since);
            cJSON_AddNumberToObject(o, "next", (double)ns);
            cJSON_AddNumberToObject(o, "n", m);
            cJSON_AddStringToObject(o, "data", out);
            *status = 200; *json = json_dup(o); return 1;
        }
        if (!strcmp(rest, "/write") && method && !strcmp(method, "POST")) {
            cJSON *j = cJSON_Parse(body ? body : "{}");
            cJSON *t = j ? cJSON_GetObjectItem(j, "text") : NULL;
            if (!cJSON_IsString(t) || !t->valuestring) {
                cJSON_Delete(j); *status = 400; *json = _strdup("{\"ok\":false,\"msg\":\"text required\"}"); return 1;
            }
            if (s->locked) { cJSON_Delete(j); *status = 423; *json = _strdup("{\"ok\":false,\"msg\":\"input locked\"}"); return 1; }
            yt_session_write(s, t->valuestring, (int)strlen(t->valuestring));
            cJSON_Delete(j);
            *status = 200; *json = _strdup("{\"ok\":true}"); return 1;
        }
        if (!strcmp(rest, "/keys") && method && !strcmp(method, "POST")) {
            cJSON *j = cJSON_Parse(body ? body : "{}");
            cJSON *t = j ? cJSON_GetObjectItem(j, "spec") : NULL;
            char exp[4096]; int n;
            if (!cJSON_IsString(t) || !t->valuestring) {
                cJSON_Delete(j); *status = 400; *json = _strdup("{\"ok\":false,\"msg\":\"spec required\"}"); return 1;
            }
            if (s->locked) { cJSON_Delete(j); *status = 423; *json = _strdup("{\"ok\":false,\"msg\":\"input locked\"}"); return 1; }
            n = yt_keys_expand(t->valuestring, exp, (int)sizeof(exp));
            if (n > 0) yt_session_write(s, exp, n);
            cJSON_Delete(j);
            *status = 200; *json = _strdup("{\"ok\":true}"); return 1;
        }
        if (!strcmp(rest, "/exec") && method && !strcmp(method, "POST")) {
            cJSON *j = cJSON_Parse(body ? body : "{}");
            cJSON *c = j ? cJSON_GetObjectItem(j, "cmd") : NULL;
            cJSON *tm = j ? cJSON_GetObjectItem(j, "timeout_ms") : NULL;
            int to = 8000, st;
            char *out = NULL;
            if (!cJSON_IsString(c) || !c->valuestring) {
                cJSON_Delete(j); *status = 400; *json = _strdup("{\"ok\":false,\"msg\":\"cmd required\"}"); return 1;
            }
            if (cJSON_IsNumber(tm)) to = (int)tm->valuedouble;
            if (s->locked) { cJSON_Delete(j); *status = 423; *json = _strdup("{\"ok\":false,\"msg\":\"input locked\"}"); return 1; }
            st = do_exec(s, c->valuestring, to, &out);
            cJSON_Delete(j);
            *status = st; *json = out; return 1;
        }
        *status = 404; *json = _strdup("{\"ok\":false,\"msg\":\"unknown channel action; GET /api/help\"}"); return 1;
    }
    return 0;
}
