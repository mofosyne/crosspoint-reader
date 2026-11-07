"""Drive and inspect a Sticky running the CROSSPOINT_SERIAL_INPUT debug firmware.

Needs pyserial and Pillow. Opening the port resets the board; the firmware then boots to Home.

  python3 sticky_console.py shot out.png          # screenshot (about 5 s over the UART)
  python3 sticky_console.py state                 # the current screen as text
  python3 sticky_console.py do "ROW:2" "PAGE:5"   # direct commands (see Activity::debugCommand)
  python3 sticky_console.py serve [port]          # web GUI: live screen, click to tap, buttons, commands

As a library:  with Sticky() as dev: dev.zim("/zim/x.zim"); print(dev.state())
"""
import glob
import io
import os
import sys
import threading
import time

import serial
from PIL import Image

BUTTONS = ("BACK", "CONFIRM", "LEFT", "RIGHT", "UP", "DOWN")
W, H = 480, 800  # portrait screenshot size


class Sticky:
    def __init__(self, port=None, baud=115200, logfile=None):
        port = port or os.environ.get("STICKY_PORT") or (sorted(glob.glob("/dev/ttyACM*")) or [None])[0]
        if not port:
            raise RuntimeError("no /dev/ttyACM* port")
        self.ser = serial.Serial()
        self.ser.port, self.ser.baudrate, self.ser.timeout = port, baud, 0.1
        self.ser.dtr = self.ser.rts = False
        self.ser.open()
        self.log = []  # text lines seen on the console
        self.logfile = open(logfile, "a") if logfile else None
        self._cv = threading.Condition()
        self._frames = {"STATE": None, "DO": None, "SHOT": None}
        self._count = {"STATE": 0, "DO": 0, "SHOT": 0}
        self._stop = False
        self._thread = threading.Thread(target=self._read, daemon=True)
        self._thread.start()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    def close(self):
        self._stop = True
        self._thread.join(timeout=2)
        self.ser.close()

    # ---- reader: splits the stream into log lines, text frames and the binary screenshot
    def _publish(self, kind, value):
        with self._cv:
            self._frames[kind] = value
            self._count[kind] += 1
            self._cv.notify_all()

    def _read(self):
        buf = b""
        while not self._stop:
            try:
                data = self.ser.read(8192)
            except Exception:
                time.sleep(0.2)
                continue
            if not data:
                continue
            buf += data
            while True:
                buf, again = self._parse(buf)
                if not again:
                    break

    def _parse(self, buf):
        """Consume one complete item from `buf`. Returns (rest, found_something)."""
        # binary screenshot: header line, N raw bytes, trailer
        i = buf.find(b"SCREENSHOT_START:")
        if i >= 0:
            j = buf.find(b"\n", i)
            if j < 0:
                return buf, False
            n = int(buf[i + 17 : j])
            if len(buf) < j + 1 + n:
                return buf, False
            self._text(buf[:i])
            self._publish("SHOT", buf[j + 1 : j + 1 + n])
            return buf[j + 1 + n :], True
        # text frames: STATE_START ... STATE_END and DO_START ... DO_END
        for kind in ("STATE", "DO"):
            start_tag = (kind + "_START").encode()
            s = buf.find(start_tag)
            if s < 0:
                continue
            e = buf.find((kind + "_END").encode(), s)
            if e < 0:
                return buf, False  # frame still arriving: wait for the rest
            self._text(buf[:s])
            body = buf[s + len(start_tag) : e].decode("utf8", "replace").replace("\r", "")
            self._publish(kind, body.strip("\n"))
            return buf[e + len(kind) + 4 :], True
        # plain log lines: flush the complete ones
        cut = buf.rfind(b"\n")
        if cut >= 0:
            self._text(buf[: cut + 1])
            return buf[cut + 1 :], False
        return buf, False

    def _text(self, raw):
        for line in raw.decode("utf8", "replace").replace("\r", "").split("\n"):
            if line.strip():
                self.log.append(line)
                if self.logfile:
                    self.logfile.write(line + "\n")
                    self.logfile.flush()

    # ---- commands
    def _ask(self, kind, command, timeout):
        with self._cv:
            before = self._count[kind]
        self.ser.write(("CMD:" + command + "\n").encode())
        self.ser.flush()
        with self._cv:
            if not self._cv.wait_for(lambda: self._count[kind] > before, timeout):
                raise TimeoutError("no reply to " + command)
            return self._frames[kind]

    def wait_ready(self, timeout=60):
        """Wait for the board to answer STATE after the reset that opening the port causes."""
        end = time.time() + timeout
        while time.time() < end:
            try:
                self.state(timeout=2)
                time.sleep(1)  # let the first screen finish drawing
                return
            except TimeoutError:
                pass
        raise TimeoutError("board did not answer STATE")

    def state(self, timeout=10):
        return self._ask("STATE", "STATE", timeout)

    def do(self, command, timeout=30):
        """Run a screen command. Returns True when the screen handled it."""
        return self._ask("DO", "DO:" + command, timeout) == "ok"

    def zim(self, path, timeout=30):
        return self._ask("DO", "ZIM:" + path, timeout) == "ok"

    def button(self, name):
        assert name in BUTTONS
        self.ser.write(("CMD:BTN:" + name + "\n").encode())

    def tap(self, x, y):
        """Tap at screenshot pixels (480x800 portrait)."""
        self.ser.write(("CMD:TAP:%.4f,%.4f\n" % (y / 800.0, 1 - x / 480.0)).encode())

    def screenshot(self, timeout=20):
        raw = self._ask("SHOT", "SCREENSHOT", timeout)
        return Image.frombytes("1", (800, 480), raw).rotate(270, expand=True).convert("L")

    def wait_state(self, text, timeout=30, poll=0.7):
        """Poll STATE until `text` appears in it; returns the state."""
        end = time.time() + timeout
        last = ""
        while time.time() < end:
            try:
                last = self.state(timeout=5)
            except TimeoutError:
                last = ""
            if text in last:
                return last
            time.sleep(poll)
        raise TimeoutError("waited for %r, last state:\n%s" % (text, last))


# ---------------------------------------------------------------------------------------- web GUI
PAGE = """<!doctype html><meta charset=utf-8><title>Sticky console</title>
<style>body{font:14px system-ui;margin:16px;display:flex;gap:16px;flex-wrap:wrap}
#screen{border:2px solid #333;cursor:pointer;width:480px;height:800px;background:#eee}
button{margin:2px;padding:6px 10px}pre{background:#111;color:#cfc;padding:8px;max-width:560px;max-height:420px;overflow:auto;white-space:pre-wrap}
input{width:360px;padding:5px}</style>
<div><img id=screen src=/shot.png width=480 height=800><div id=msg>click the screen to tap</div></div>
<div><div>
 <button onclick="btn('BACK')">Back</button><button onclick="btn('UP')">Up</button><button onclick="btn('DOWN')">Down</button>
 <button onclick="btn('LEFT')">Left</button><button onclick="btn('RIGHT')">Right</button><button onclick="btn('CONFIRM')">Confirm</button>
 <button onclick="refresh()">Refresh</button>
</div><div><input id=cmd placeholder="DO command, e.g. SEARCH:moon or ROW:2 or PAGE:5"> <button onclick="doCmd()">Run</button></div>
<h4>Screen state</h4><pre id=state></pre></div>
<script>
const img=document.getElementById('screen');
async function post(u){document.getElementById('msg').textContent='working...';await fetch(u,{method:'POST'});setTimeout(refresh,1500)}
function btn(n){post('/btn?name='+n)}
function doCmd(){post('/do?cmd='+encodeURIComponent(document.getElementById('cmd').value))}
img.onclick=e=>{const r=img.getBoundingClientRect();post('/tap?x='+Math.round((e.clientX-r.left)*480/r.width)+'&y='+Math.round((e.clientY-r.top)*800/r.height))};
async function refresh(){document.getElementById('msg').textContent='reading screen...';
 const s=fetch('/state').then(r=>r.text()).then(t=>document.getElementById('state').textContent=t);
 img.src='/shot.png?'+Date.now();img.onload=()=>document.getElementById('msg').textContent='ok';await s}
refresh();
</script>"""


def serve(dev, port=8765):
    from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
    from urllib.parse import parse_qs, urlparse

    lock = threading.Lock()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def _send(self, body, ctype):
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            u = urlparse(self.path)
            with lock:
                if u.path == "/":
                    self._send(PAGE.encode(), "text/html")
                elif u.path == "/shot.png":
                    buf = io.BytesIO()
                    dev.screenshot().save(buf, "PNG")
                    self._send(buf.getvalue(), "image/png")
                elif u.path == "/state":
                    try:
                        self._send(dev.state().encode(), "text/plain; charset=utf-8")
                    except TimeoutError:
                        self._send(b"(no reply)", "text/plain")
                else:
                    self.send_error(404)

        def do_POST(self):
            u = urlparse(self.path)
            q = {k: v[0] for k, v in parse_qs(u.query).items()}
            with lock:
                if u.path == "/tap":
                    dev.tap(int(q["x"]), int(q["y"]))
                elif u.path == "/btn":
                    dev.button(q["name"])
                elif u.path == "/do":
                    try:
                        dev.do(q["cmd"])
                    except TimeoutError:
                        pass
            self._send(b"ok", "text/plain")

    print("Sticky console on http://localhost:%d/" % port)
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()


if __name__ == "__main__":
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        sys.exit(2)
    with Sticky() as dev:
        dev.wait_ready()  # the open reset the board
        if args[0] == "shot":
            dev.screenshot().save(args[1] if len(args) > 1 else "shot.png")
        elif args[0] == "state":
            print(dev.state())
        elif args[0] == "do":
            for c in args[1:]:
                print(c, "->", dev.do(c))
        elif args[0] == "serve":
            serve(dev, int(args[1]) if len(args) > 1 else 8765)
        else:
            print(__doc__)
