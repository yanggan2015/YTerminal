#!/usr/bin/env python3
"""YTerminal 全量 Bridge 回归（win32-ai-ui-bridge / ui_probe）"""
import json
import os
import subprocess
import sys
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, r"C:\code\win32-ai-ui-bridge\win32-ai-ui-bridge\scripts")
from ui_probe import Bridge, BridgeError  # noqa: E402

EXE = os.path.join(ROOT, "build", "YTerminal.exe")
BASE = "http://127.0.0.1:8086"
PASS = FAIL = 0
REPORT = []


def ok(name, detail=""):
    global PASS
    PASS += 1
    REPORT.append(("PASS", name, detail))
    print(f"[PASS] {name}" + (f" | {detail}" if detail else ""))


def bad(name, detail=""):
    global FAIL
    FAIL += 1
    REPORT.append(("FAIL", name, detail))
    print(f"[FAIL] {name} | {detail}")


def biz(br):
    """业务 state 嵌在 /state 的 state 字段里"""
    return br.state().get("state") or {}


def alive(timeout=2.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            with urllib.request.urlopen(BASE + "/state", timeout=1.5) as r:
                if r.status == 200:
                    return True
        except Exception:
            time.sleep(0.1)
    return False


def wait_biz(br, key, want, timeout=5.0):
    ok_w, last = br.wait(f"state.{key}", want, timeout=timeout)
    if not ok_w:
        raise AssertionError(f"wait state.{key}=={want} got={(last or {}).get('state')}")
    return last


def kill_old():
    subprocess.run(["taskkill", "/F", "/IM", "YTerminal.exe"], capture_output=True)
    time.sleep(0.4)


def start_app():
    kill_old()
    subprocess.Popen([EXE], cwd=os.path.dirname(EXE))
    for _ in range(50):
        try:
            br = Bridge(BASE, timeout=3)
            if br.ping():
                time.sleep(0.4)
                return br
        except Exception:
            pass
        time.sleep(0.2)
    raise SystemExit("bridge ping timeout")


def close_all(br, label="close_all"):
    """点断开直到 tabs=0；任一步超时=UI 卡死（用户报的死机）"""
    for i in range(12):
        n = biz(br).get("tabs", 0)
        if n <= 0:
            ok(label, f"tabs=0 after {i} closes")
            return True
        t0 = time.time()
        try:
            br.click("btn.close")
        except BridgeError as e:
            bad(f"{label} click#{i}", f"{e} elapsed={time.time()-t0:.2f}s")
            return False
        if not alive(3.0):
            bad(f"{label} hang#{i}", f"UI frozen after close, had tabs={n}")
            return False
        # tabs 应下降
        try:
            wait_biz(br, "tabs", n - 1, timeout=3)
        except AssertionError as e:
            # 可能异步已关完
            if biz(br).get("tabs", 0) == 0:
                continue
            bad(f"{label} tabs#{i}", str(e))
            return False
    bad(label, f"still tabs={biz(br).get('tabs')}")
    return False


def case_tree(br):
    ids = {c.get("id") for c in br.tree().get("controls", []) if c.get("id")}
    need = ["cmb.kind", "btn.connect", "btn.close", "btn.local", "tab.main", "edit.ssh.host"]
    miss = [x for x in need if x not in ids]
    if miss:
        bad("ui-tree ids", f"missing {miss}")
    else:
        ok("ui-tree ids", f"n={len(ids)}")


def case_startup_local_and_disconnect(br):
    """启动自带 Local 页；右键断开等价 btn.close — 必须不卡死"""
    st = biz(br)
    if st.get("tabs", 0) < 1:
        br.click("btn.local")
        try:
            wait_biz(br, "tabs", 1, timeout=5)
        except AssertionError as e:
            bad("startup local", str(e))
            return
    else:
        ok("startup has local tab", json.dumps(st, ensure_ascii=False))
    close_all(br, "startup disconnect")


def case_local_x3(br):
    for i in range(3):
        before = biz(br).get("tabs", 0)
        br.act("cmb.kind", "select", value=0)
        br.click("btn.connect")
        try:
            wait_biz(br, "tabs", before + 1, timeout=5)
            wait_biz(br, "kind", 0, timeout=2)
            wait_biz(br, "state", 2, timeout=2)
        except AssertionError as e:
            bad(f"local connect#{i}", str(e))
            return
        if not close_all(br, f"local disc#{i}"):
            return
    ok("local connect/disconnect x3")


def case_btn_local(br):
    before = biz(br).get("tabs", 0)
    br.click("btn.local")
    try:
        wait_biz(br, "tabs", before + 1, timeout=5)
        wait_biz(br, "kind", 0, timeout=2)
        ok("btn.local")
    except AssertionError as e:
        bad("btn.local", str(e))
        return
    close_all(br, "btn.local disconnect")


def case_ssh(br):
    close_all(br, "pre-ssh clear")
    br.act("cmb.kind", "select", value=1)
    br.set_text("edit.ssh.host", "sshbridge.online")
    br.set_text("edit.ssh.port", "52222")
    br.set_text("edit.ssh.user", "demo")
    br.set_text("edit.ssh.pass", "123456")
    br.click("btn.connect")
    t0 = time.time()
    connected = False
    while time.time() - t0 < 20:
        st = biz(br)
        if st.get("tabs", 0) >= 1 and st.get("state") == 2 and st.get("kind") == 1:
            ok("ssh connect", json.dumps(st, ensure_ascii=False))
            connected = True
            break
        if st.get("tabs", 0) == 0 and time.time() - t0 > 10:
            bad("ssh connect", f"failed early {st}")
            return
        time.sleep(0.35)
    if not connected:
        bad("ssh connect", f"timeout {biz(br)}")
        return
    close_all(br, "ssh disconnect")


def case_serial(br, com="COM5", baud="115200"):
    close_all(br, "pre-serial clear")
    br.act("cmb.kind", "select", value=2)
    try:
        br.set_text("edit.serial.com", com)
        br.set_text("edit.serial.baud", baud)
    except BridgeError as e:
        bad("serial fields", str(e))
        return
    br.click("btn.connect")
    try:
        wait_biz(br, "tabs", 1, timeout=5)
        st = biz(br)
        if st.get("state") != 2 or st.get("kind") != 2:
            bad("serial connect", json.dumps(st, ensure_ascii=False))
            close_all(br, "serial fail cleanup")
            return
        ok("serial connect", json.dumps(st, ensure_ascii=False))
    except AssertionError as e:
        bad("serial connect", str(e))
        return
    close_all(br, "serial disconnect")


def case_multi_then_close(br):
    """连 2 个 Local + 关干净"""
    close_all(br, "pre-multi clear")
    for _ in range(2):
        br.click("btn.local")
        time.sleep(0.3)
    try:
        wait_biz(br, "tabs", 2, timeout=5)
        ok("multi local tabs=2")
    except AssertionError as e:
        bad("multi local", str(e))
        return
    close_all(br, "multi disconnect")


def main():
    br = start_app()
    case_tree(br)
    case_startup_local_and_disconnect(br)
    case_local_x3(br)
    case_btn_local(br)
    case_multi_then_close(br)
    case_ssh(br)
    case_serial(br)
    if br.ping() and alive(2):
        ok("final ping")
    else:
        bad("final ping", "dead")

    out = os.path.join(ROOT, "docs", "bridge_test_report.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump(
            {"pass": PASS, "fail": FAIL, "cases": [{"r": a, "name": b, "detail": c} for a, b, c in REPORT]},
            f,
            ensure_ascii=False,
            indent=2,
        )
    print(f"\n=== TOTAL pass={PASS} fail={FAIL} ===")
    kill_old()
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
