#!/usr/bin/env python3
#----- Animator watch -----||
"""Compile a scene to WebAssembly and serve the viewer with live reload.

    python anim.py watch path/to/scene.cpp
    python anim.py watch path/to/scene.cpp --port 8080
"""

import json
import os
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)

USAGE = """\
usage: anim.py watch <scene.cpp> [--port N]

Compiles the scene with emcc, serves the viewer on 0.0.0.0, and rebuilds
when the scene or a quoted include changes. The page reloads itself.
"""

state_lock = threading.Lock()
state = {"ok": False, "rev": 0, "error": "", "building": True}


def set_state(**kw):
    with state_lock:
        state.update(kw)


def get_state():
    with state_lock:
        return dict(state)


def find_emcc():
    env = os.environ.get("EMCC")
    if env:
        return env
    found = shutil.which("emcc")
    if found:
        return found
    home = Path.home() / "emsdk" / "upstream" / "emscripten" / "emcc"
    if home.is_file():
        return str(home)
    return "emcc"


def include_dirs(scene):
    dirs = [
        ROOT / "engine",
        ROOT / "models",
        ROOT / "models" / "examples",
        scene.parent,
        ROOT,
    ]
    out = []
    seen = set()
    for d in dirs:
        key = str(d.resolve()) if d.exists() else str(d)
        if key in seen:
            continue
        seen.add(key)
        out.append(d)
    return out


def resolve_include(src, rel, inc_dirs):
    cand = (src.parent / rel)
    if cand.is_file():
        return cand.resolve()
    for d in inc_dirs:
        cand = d / rel
        if cand.is_file():
            return cand.resolve()
    return None


def collect_sources(scene, inc_dirs):
    found = []
    seen = set()
    stack = [scene.resolve()]
    while stack:
        path = stack.pop()
        if path in seen or not path.is_file():
            continue
        seen.add(path)
        found.append(path)
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for match in INCLUDE_RE.finditer(text):
            nxt = resolve_include(path, match.group(1), inc_dirs)
            if nxt is not None:
                stack.append(nxt)
    return found


def source_stamp(paths):
    stamp = []
    for path in paths:
        try:
            stamp.append((str(path), path.stat().st_mtime_ns))
        except OSError:
            stamp.append((str(path), 0))
    return tuple(stamp)


def lan_ips():
    ips = []
    try:
        out = subprocess.check_output(["ip", "-4", "-o", "addr", "show"], text=True, stderr=subprocess.DEVNULL)
        for line in out.splitlines():
            match = re.search(r"inet (\d+\.\d+\.\d+\.\d+)/", line)
            if match and not match.group(1).startswith("127."):
                ips.append(match.group(1))
    except (OSError, subprocess.CalledProcessError):
        pass
    if not ips:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.connect(("8.8.8.8", 80))
            ip = sock.getsockname()[0]
            sock.close()
            if not ip.startswith("127."):
                ips.append(ip)
        except OSError:
            pass
    unique = []
    for ip in ips:
        if ip not in unique:
            unique.append(ip)
    return unique


def compile_scene(scene, inc_dirs):
    BUILD.mkdir(parents=True, exist_ok=True)
    emcc = find_emcc()
    out_js = BUILD / "next.js"
    out_wasm = BUILD / "next.wasm"
    cmd = [
        emcc,
        str(scene),
        str(ROOT / "viewer" / "anim_gl.cpp"),
        "-std=c++17",
        "-Wall",
        "-Wno-unused-function",
        "-O1",
        "-o",
        str(out_js),
        "-s",
        "MODULARIZE=1",
        "-s",
        "EXPORT_NAME=createAnim",
        "-s",
        "ENVIRONMENT=web",
        "-s",
        "INVOKE_RUN=0",
        "-s",
        "MIN_WEBGL_VERSION=2",
        "-s",
        "MAX_WEBGL_VERSION=2",
        "-s",
        "ALLOW_MEMORY_GROWTH=1",
        "-s",
        "EXPORTED_RUNTIME_METHODS=['UTF8ToString']",
        "-s",
        "STACK_SIZE=1048576",
    ]
    for d in inc_dirs:
        cmd.extend(["-I", str(d)])
    print("compiling " + scene.name, flush=True)
    try:
        proc = subprocess.run(cmd, cwd=str(ROOT), capture_output=True, text=True)
    except OSError as err:
        return 1, "Could not run emcc (%s).\nInstall emsdk and activate it, or set EMCC.\n%s\n" % (emcc, err)
    log = (proc.stderr or "") + (proc.stdout or "")
    if len(log) > 48000:
        log = log[-48000:]
    if proc.returncode == 0 and out_js.is_file() and out_wasm.is_file():
        os.replace(out_js, BUILD / "scene.js")
        os.replace(out_wasm, BUILD / "scene.wasm")
        return 0, log
    for leftover in (out_js, out_wasm):
        try:
            leftover.unlink()
        except OSError:
            pass
    if proc.returncode == 0:
        log += "\nemcc reported success but did not write the wasm output\n"
        return 1, log
    return proc.returncode, log


class Handler(BaseHTTPRequestHandler):
    server_version = "Animator"

    def log_message(self, fmt, *args):
        return

    def _send(self, code, body, content_type):
        data = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/api/status":
            self._send(200, json.dumps(get_state()), "application/json")
            return
        files = {
            "/": ROOT / "viewer" / "index.html",
            "/index.html": ROOT / "viewer" / "index.html",
            "/viewer.js": ROOT / "viewer" / "viewer.js",
            "/scene.js": BUILD / "scene.js",
            "/scene.wasm": BUILD / "scene.wasm",
        }
        target = files.get(path)
        if target is None or not target.is_file():
            self._send(404, "not found\n", "text/plain; charset=utf-8")
            return
        if path.endswith(".wasm"):
            ctype = "application/wasm"
        elif path.endswith(".js"):
            ctype = "text/javascript; charset=utf-8"
        elif path.endswith(".html") or path == "/":
            ctype = "text/html; charset=utf-8"
        else:
            ctype = "application/octet-stream"
        try:
            payload = target.read_bytes()
        except OSError:
            self._send(404, "not found\n", "text/plain; charset=utf-8")
            return
        self._send(200, payload, ctype)


def serve(port):
    httpd = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    httpd.daemon_threads = True
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    return httpd


def pick_port(preferred):
    for port in range(preferred, preferred + 20):
        try:
            httpd = serve(port)
            return port, httpd
        except OSError:
            continue
    sys.exit("no free port from %d" % preferred)


def parse(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help", "help"):
        print(USAGE, end="")
        sys.exit(0 if len(argv) > 1 and argv[1] in ("-h", "--help", "help") else 1)
    if argv[1] != "watch" or len(argv) < 3:
        print(USAGE, end="")
        sys.exit(1)
    scene = Path(argv[2]).resolve()
    port = 8080
    i = 3
    while i < len(argv):
        if argv[i] == "--port" and i + 1 < len(argv):
            port = int(argv[i + 1])
            i += 2
        else:
            sys.exit("unknown argument: " + argv[i])
    if not scene.is_file():
        sys.exit("scene not found: " + str(scene))
    return scene, port


def main():
    scene, preferred = parse(sys.argv)
    port, _httpd = pick_port(preferred)
    incs = include_dirs(scene)
    print("Animator", flush=True)
    print("  local:   http://127.0.0.1:%d/" % port, flush=True)
    ips = lan_ips()
    if ips:
        for ip in ips:
            print("  network: http://%s:%d/" % (ip, port), flush=True)
    else:
        print("  network: (no LAN address found)", flush=True)
    print("watching " + str(scene), flush=True)

    stamp = None
    while True:
        files = collect_sources(scene, incs)
        now = source_stamp(files)
        if now != stamp:
            if stamp is not None:
                # The editor may still be writing. Wait briefly, then rescan.
                time.sleep(0.2)
                files = collect_sources(scene, incs)
                now = source_stamp(files)
            set_state(building=True)
            code, log = compile_scene(scene, incs)
            if code != 0:
                sys.stdout.write(log)
                if log and not log.endswith("\n"):
                    sys.stdout.write("\n")
                print("compile failed", flush=True)
                set_state(ok=False, error=log, building=False)
            else:
                if log.strip():
                    sys.stdout.write(log)
                    if not log.endswith("\n"):
                        sys.stdout.write("\n")
                rev = get_state()["rev"] + 1
                set_state(ok=True, rev=rev, error="", building=False)
                print("ready (rev %d), %d files" % (rev, len(files)), flush=True)
            stamp = now
        time.sleep(0.25)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nstopped")
        sys.exit(0)
