#!/usr/bin/env python3
"""tools/ci-runner/status.py <address> <host.sh> [machine...]: which of our runners are free, as JSON over HTTP
on <address>:8790 (omarchy's fabric address; the others' firewalls keep the port closed), for the changes job's
Free runners step (build.yml), which sends a job to GitHub's runners when ours are busy or off, so that no job
waits for ours:

  {"linux": n, "windows": "idle|busy|off"}

  linux:    the idle goath1 runners: this machine's (host.sh status), and each linux:<host>'s, over ssh
  windows:  windows:<host>'s Windows VM runner (tools/ci-runner/windows.ps1 status); off while the VM is
            stopped (this only looks: it never starts the VM)

Read-only, polled every 5 s in the background, so that an answer takes no time."""
import http.server
import json
import subprocess
import sys
import threading
import time
from pathlib import Path

VM = "halo@192.168.122.119"
HOME = Path.home()
SSH = ["ssh", "-F", str(HOME / ".ssh/fabric.conf"), "-o", f"UserKnownHostsFile={HOME / '.ssh/known_hosts.fabric'}",
       "-o", "StrictHostKeyChecking=yes", "-o", "BatchMode=yes", "-o", "ConnectTimeout=3"]
address, host_sh, *machines = sys.argv[1:]
state = {"linux": 0, "windows": "off"}


def run(command, timeout=20):
    return subprocess.run(command, capture_output=True, text=True, timeout=timeout).stdout.strip()


def linux(host):
    try:
        out = run([host_sh, "status"]) if host is None else run([*SSH, host, "goat-rig/ci/bin/host.sh status"])
        return int(out)
    except Exception:
        return 0


def windows(host):
    script = ("[ \"$(virsh -c qemu:///system domstate halo-win 2>/dev/null)\" = running ] && "
              f"ssh -o BatchMode=yes -o ConnectTimeout=3 {VM} "
              "'powershell -NoProfile -ExecutionPolicy Bypass -File C:\\goath1-ci\\windows.ps1 status'")
    try:
        out = run([*SSH, host, script])
    except Exception:
        return "off"
    return out if out in ("idle", "busy") else "off"


def poll():
    while True:
        state["linux"] = linux(None) + sum(linux(m.split(":", 1)[1]) for m in machines if m.startswith("linux:"))
        found = [windows(m.split(":", 1)[1]) for m in machines if m.startswith("windows:")]
        state["windows"] = "idle" if "idle" in found else "busy" if "busy" in found else "off"
        time.sleep(5)


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = json.dumps(state).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


threading.Thread(target=poll, daemon=True).start()
http.server.ThreadingHTTPServer((address, 8790), Handler).serve_forever()
