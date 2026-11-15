#!/usr/bin/env python3
"""ui_probe.py — 与 win_ui_bridge 桥接层通信的探针 CLI（仅标准库）。

AI Agent 侧的最小工具集：读界面树、读业务状态、点控件、看日志、做 UI 回归快照。
也可作为模块 import：from ui_probe import Bridge

常用：
    python3 ui_probe.py tree                 # 看界面树
    python3 ui_probe.py state                # 看内部状态
    python3 ui_probe.py click btn.add        # 点按钮
    python3 ui_probe.py set-text edit.input 12
    python3 ui_probe.py wait state.sum 12 --timeout 5   # 断言状态
    python3 ui_probe.py logs --since 0
    python3 ui_probe.py save baseline.json   # 存基线
    python3 ui_probe.py diff baseline.json   # 回归比对
"""
import argparse
import json
import sys
import time
import urllib.error
import urllib.request

DEFAULT_BASE = "http://127.0.0.1:8086"


class BridgeError(RuntimeError):
    pass


class Bridge:
    def __init__(self, base=DEFAULT_BASE, token=None, timeout=10):
        self.base = base.rstrip("/")
        self.token = token
        self.timeout = timeout

    # ---------------------------------------------------------------- HTTP
    def _call(self, method, path, payload=None):
        url = self.base + path
        data = None
        headers = {"Accept": "application/json"}
        if payload is not None:
            data = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"
        if self.token:
            headers["X-Ui-Token"] = self.token
        req = urllib.request.Request(url, data=data, headers=headers, method=method)
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                body = resp.read().decode("utf-8")
        except urllib.error.HTTPError as e:
            body = e.read().decode("utf-8", "replace")
            try:
                return e.code, json.loads(body) if body.strip() else {}
            except json.JSONDecodeError:
                return e.code, {"raw": body}
        except urllib.error.URLError as e:
            raise BridgeError(f"连不上桥接层 {url}：{e.reason}（程序是否在运行、端口是否正确？）")
        return 200, json.loads(body) if body.strip() else {}

    # -------------------------------------------------------------- 只读接口
    def tree(self):
        code, data = self._call("GET", "/ui-tree")
        if code != 200:
            raise BridgeError(f"GET /ui-tree -> {code} {data}")
        return data

    def state(self):
        code, data = self._call("GET", "/state")
        if code != 200:
            raise BridgeError(f"GET /state -> {code} {data}")
        return data

    def logs(self, since=0):
        code, data = self._call("GET", f"/logs?since={int(since)}")
        if code != 200:
            raise BridgeError(f"GET /logs -> {code} {data}")
        return data

    def ping(self):
        code, data = self._call("GET", "/ping")
        return code == 200 and data.get("ok") is True

    # -------------------------------------------------------------- 动作接口
    def act(self, cid, action="click", text=None, value=None):
        payload = {"id": cid, "action": action}
        if text is not None:
            payload["text"] = text
        if value is not None:
            payload["value"] = value
        code, data = self._call("POST", "/action", payload)
        if code != 200 or not data.get("ok"):
            raise BridgeError(f"动作失败 {cid} {action}: {code} {data.get('message')}")
        return data

    def click(self, cid):
        return self.act(cid, "click")

    def set_text(self, cid, text):
        return self.act(cid, "set_text", text=text)

    def select(self, cid, index):
        return self.act(cid, "select", value=index)

    def command(self, cmd_id):
        return self.act("", "command", value=cmd_id)

    # -------------------------------------------------------------- 断言辅助
    def find(self, cid):
        for c in self.tree().get("controls", []):
            if c.get("id") == cid:
                return c
        return None

    @staticmethod
    def dig(obj, path, default=None):
        """按 a.b.0.c 取 JSON 路径。"""
        cur = obj
        for part in path.split("."):
            if isinstance(cur, list):
                try:
                    cur = cur[int(part)]
                except (ValueError, IndexError):
                    return default
            elif isinstance(cur, dict):
                if part not in cur:
                    return default
                cur = cur[part]
            else:
                return default
        return cur

    def wait(self, path, expect, timeout=3.0, interval=0.1):
        """轮询 /state，直到路径值等于 expect。返回是否成功。"""
        deadline = time.time() + timeout
        last = None
        while time.time() < deadline:
            last = self.state()
            got = self.dig(last, path)
            if str(got) == str(expect) or got == expect:
                return True, last
            time.sleep(interval)
        return False, last


# ------------------------------------------------------------------ 快照比对
def normalize(tree, with_rect=False):
    out = {}
    for c in tree.get("controls", []):
        item = {
            "role": c.get("role"),
            "text": c.get("text"),
            "enabled": c.get("enabled"),
            "visible": c.get("visible"),
        }
        if with_rect:
            item["rect"] = c.get("rect")
        out[c.get("id")] = item
    return out


def diff_trees(old, new, with_rect=False):
    a, b = normalize(old, with_rect), normalize(new, with_rect)
    added = sorted(set(b) - set(a))
    removed = sorted(set(a) - set(b))
    changed = []
    for cid in sorted(set(a) & set(b)):
        delta = {k: (a[cid][k], b[cid][k]) for k in a[cid] if a[cid][k] != b[cid][k]}
        if delta:
            changed.append({"id": cid, "diff": delta})
    return {"added": added, "removed": removed, "changed": changed}


def print_tree(tree, full=False):
    controls = tree.get("controls", [])
    print(f"{len(controls)} 个控件（main_rect={tree.get('main_rect')}）")
    for c in controls:
        if not full and not c.get("visible", True):
            continue
        flag = "" if c.get("enabled", True) else " [disabled]"
        parent = "" if c.get("parent") in (None, "root") else f"  <{c['parent']}>"
        print(f"  {c.get('id'):<32} {c.get('role'):<10} \"{c.get('text','')}\"{flag}{parent}")


def main(argv=None):
    ap = argparse.ArgumentParser(description="win_ui_bridge 探针")
    ap.add_argument("--base", default=DEFAULT_BASE)
    ap.add_argument("--token", default=None)
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("ping")
    sub.add_parser("tree").add_argument("--full", action="store_true")
    sub.add_parser("state")
    p_logs = sub.add_parser("logs")
    p_logs.add_argument("--since", type=int, default=0)

    p_click = sub.add_parser("click")
    p_click.add_argument("id")
    p_text = sub.add_parser("set-text")
    p_text.add_argument("id")
    p_text.add_argument("text")
    p_sel = sub.add_parser("select")
    p_sel.add_argument("id")
    p_sel.add_argument("index", type=int)
    p_cmd = sub.add_parser("command")
    p_cmd.add_argument("cmd_id", type=int)

    p_wait = sub.add_parser("wait")
    p_wait.add_argument("path")
    p_wait.add_argument("expect")
    p_wait.add_argument("--timeout", type=float, default=3.0)

    p_save = sub.add_parser("save")
    p_save.add_argument("path")
    p_diff = sub.add_parser("diff")
    p_diff.add_argument("path")

    args = ap.parse_args(argv)
    br = Bridge(args.base, args.token)

    if args.cmd == "ping":
        ok = br.ping()
        print("bridge ok" if ok else "bridge 未响应")
        return 0 if ok else 2
    if args.cmd == "tree":
        t = br.tree()
        if args.full:
            print(json.dumps(t, ensure_ascii=False, indent=2))
        else:
            print_tree(t)
        return 0
    if args.cmd == "state":
        print(json.dumps(br.state(), ensure_ascii=False, indent=2))
        return 0
    if args.cmd == "logs":
        d = br.logs(args.since)
        for e in d.get("entries", []):
            print(f"[{e['seq']:>4}] {e['text']}")
        print(f"(next_since={d.get('next_since')})")
        return 0
    if args.cmd == "click":
        print(json.dumps(br.click(args.id), ensure_ascii=False))
        return 0
    if args.cmd == "set-text":
        print(json.dumps(br.set_text(args.id, args.text), ensure_ascii=False))
        return 0
    if args.cmd == "select":
        print(json.dumps(br.select(args.id, args.index), ensure_ascii=False))
        return 0
    if args.cmd == "command":
        print(json.dumps(br.command(args.cmd_id), ensure_ascii=False))
        return 0
    if args.cmd == "wait":
        ok, last = br.wait(args.path, args.expect, args.timeout)
        print(("满足" if ok else "超时未满足") + f"：{args.path} = {Bridge.dig(last, args.path)}")
        return 0 if ok else 1
    if args.cmd == "save":
        snap = br.tree()
        with open(args.path, "w", encoding="utf-8") as f:
            json.dump(snap, f, ensure_ascii=False, indent=2)
        print(f"已保存基线 {args.path}（{len(snap.get('controls', []))} 个控件）")
        return 0
    if args.cmd == "diff":
        with open(args.path, encoding="utf-8") as f:
            baseline = json.load(f)
        d = diff_trees(baseline, br.tree())
        if not (d["added"] or d["removed"] or d["changed"]):
            print("UI 与基线一致")
            return 0
        print(json.dumps(d, ensure_ascii=False, indent=2))
        return 1
    return 2


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BridgeError as e:
        print(f"错误：{e}", file=sys.stderr)
        sys.exit(2)
