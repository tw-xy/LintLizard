#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
LintLizard 树莓派上位机 Web 控制服务（纯标准库，无需 pip / 无需安装）

接口：
  GET /                     控制界面（虚拟摇杆 + 摄像头 + 点云）
  GET /cmd?x=&y=            运动指令 -> 写 {"x":-100..100,"y":-100..100}\n 到串口
  GET /video                摄像头 MJPEG 流（rpicam-vid 编码）
  GET /scan                 雷达点云 JSON（角度 ° / 距离 mm）
  GET /status               状态 JSON

环境变量（都有默认值，可不设）：
  LINT_SERIAL   串口设备     默认 /dev/serial0
  LINT_BAUD     波特率       默认 115200
  LINT_PORT     HTTP 端口    默认 8080
  LINT_CAM_W    摄像头宽     默认 640
  LINT_CAM_H    摄像头高     默认 480
  LINT_CAM_FPS  摄像头帧率   默认 15

启动：
  python3 server.py
  nohup python3 server.py >/tmp/lintlizard-web.log 2>&1 &
"""

import json
import os
import subprocess
import termios
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

SERIAL_PORT = os.environ.get("LINT_SERIAL", "/dev/serial0")
SERIAL_BAUD = int(os.environ.get("LINT_BAUD", "115200"))
HTTP_PORT = int(os.environ.get("LINT_PORT", "8080"))
CAM_WIDTH = int(os.environ.get("LINT_CAM_W", "640"))
CAM_HEIGHT = int(os.environ.get("LINT_CAM_H", "480"))
CAM_FPS = int(os.environ.get("LINT_CAM_FPS", "15"))

CMD_ZERO_TIMEOUT = 0.4
START_TIME = time.time()


# --------------------------------------------------------------------------- #
# 串口
# --------------------------------------------------------------------------- #
class SerialLink:
    """把运动指令写进 /dev/serial0；顺手做一次「客户端掉线就回中」的兜底。"""

    def __init__(self, port, baud):
        self.port = port
        self.baud = baud
        self.fd = None
        self.lock = threading.Lock()
        self.last_write = 0.0
        self.tx_count = 0
        self.last_error = None
        self.stopped = True
        self.last_open_attempt = None
        self._open()

    def _open(self):
        now = time.monotonic()
        if self.last_open_attempt is not None and now - self.last_open_attempt < 1.0:
            return
        self.last_open_attempt = now
        fd = None
        try:
            fd = os.open(self.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            attrs = termios.tcgetattr(fd)
            attrs[0] = 0
            attrs[1] = 0
            attrs[2] = (attrs[2] & ~termios.CSIZE) | termios.CS8
            attrs[2] |= termios.CREAD | termios.CLOCAL
            attrs[2] &= ~(termios.CSTOPB | termios.PARENB | termios.CRTSCTS)
            attrs[3] = 0
            attrs[6][termios.VMIN] = 0
            attrs[6][termios.VTIME] = 0
            speed = getattr(termios, "B%d" % self.baud, termios.B115200)
            attrs[4] = speed
            attrs[5] = speed
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
            termios.tcflush(fd, termios.TCIOFLUSH)
            self.fd = fd
            self.last_error = None
            print("[serial] %s @ %d 8N1 已打开" % (self.port, self.baud), flush=True)
        except Exception as exc:
            if fd is not None:
                os.close(fd)
            self.fd = None
            self.last_error = str(exc)
            print("[serial] 打开 %s 失败: %s" % (self.port, exc), flush=True)

    def read(self):
        """Nonblocking read, serialized with send/reopen to protect the shared fd."""
        with self.lock:
            if self.fd is None:
                self._open()
                # Reset any partial line after a disconnect, even if reopen worked.
                return None
            try:
                return os.read(self.fd, 4096)
            except BlockingIOError:
                return b""
            except OSError as exc:
                self.last_error = str(exc)
                os.close(self.fd)
                self.fd = None
                return None

    def send(self, x, y):
        x = max(-100, min(100, int(x)))
        y = max(-100, min(100, int(y)))
        payload = ('{"x":%d,"y":%d}\n' % (x, y)).encode("ascii")
        with self.lock:
            if self.fd is None:
                self._open()
            if self.fd is None:
                return False
            try:
                os.write(self.fd, payload)
                self.tx_count += 1
                self.last_write = time.time()
                self.stopped = False
                return True
            except Exception as exc:
                self.last_error = str(exc)
                try:
                    os.close(self.fd)
                except Exception:
                    pass
                self.fd = None
                return False

    def watchdog(self):
        """浏览器关掉/断网后，补发一条中性指令让底盘停下。"""
        while True:
            time.sleep(0.05)
            with self.lock:
                if self.fd is None or self.stopped or not self.last_write:
                    continue
                if time.time() - self.last_write > CMD_ZERO_TIMEOUT:
                    try:
                        os.write(self.fd, b'{"x":0,"y":0}\n')
                        self.tx_count += 1
                    except Exception:
                        pass
                    self.stopped = True

    def status(self):
        return {
            "port": self.port,
            "baud": self.baud,
            "open": self.fd is not None,
            "tx": self.tx_count,
            "error": self.last_error,
        }


SERIAL = None  # Open hardware in main(), so importing is side-effect free.


# --------------------------------------------------------------------------- #
# 摄像头（MJPEG）
# --------------------------------------------------------------------------- #
def camera_available():
    try:
        out = subprocess.run(
            ["rpicam-hello", "--list-cameras", "--timeout", "1"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=6,
        ).stdout.decode("utf-8", "replace")
        return "No cameras available" not in out and "0 :" in out
    except Exception:
        return False


def mjpeg_frames():
    """把 rpicam-vid 的 MJPEG 流切成一帧一帧的 JPEG。"""
    proc = subprocess.Popen(
        [
            "rpicam-vid", "-t", "0", "--codec", "mjpeg", "--inline",
            "--width", str(CAM_WIDTH), "--height", str(CAM_HEIGHT),
            "--framerate", str(CAM_FPS), "--nopreview", "-o", "-",
        ],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0,
    )
    buf = b""
    try:
        while True:
            chunk = proc.stdout.read(8192)
            if not chunk:
                break
            buf += chunk
            while True:
                start = buf.find(b"\xff\xd8")
                if start < 0:
                    buf = buf[-2:]
                    break
                end = buf.find(b"\xff\xd9", start + 2)
                if end < 0:
                    buf = buf[start:]
                    break
                frame = buf[start:end + 2]
                buf = buf[end + 2:]
                header = b"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n" % len(frame)
                yield header + frame + b"\r\n"
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except Exception:
            proc.kill()


# --------------------------------------------------------------------------- #
# 雷达点云：CH32 USART3 ASCII 行流，最多保留每度的最新点
# --------------------------------------------------------------------------- #
class RadarBuffer:
    POINT_TTL = 0.5
    MAX_LINE = 32

    def __init__(self):
        self.lock = threading.Lock()
        self.points = {}
        self.pending = bytearray()
        self.discard_line = False
        self.updated = 0.0
        self.last_point = None
        self.rx_bytes = 0
        self.received = 0
        self.invalid = 0

    def reset_stream(self):
        with self.lock:
            self.pending.clear()
            self.discard_line = False
            self.points.clear()
            self.last_point = None

    def feed(self, data):
        with self.lock:
            self.rx_bytes += len(data)
            for byte in data:
                if byte == 10:
                    if not self.discard_line:
                        self._line(bytes(self.pending).rstrip(b"\r"))
                    self.pending.clear()
                    self.discard_line = False
                elif not self.discard_line:
                    if len(self.pending) >= self.MAX_LINE:
                        self.pending.clear()
                        self.discard_line = True
                        self.invalid += 1
                    else:
                        self.pending.append(byte)

    def _line(self, line):
        fields = line.split(b",")
        if (len(fields) != 3 or fields[0] != b"R" or
                not fields[1].isdigit() or not fields[2].isdigit()):
            self.invalid += 1
            return
        angle, distance = int(fields[1]), int(fields[2])
        if not (0 <= angle < 36000 and 120 <= distance <= 8000):
            self.invalid += 1
            return
        now = time.monotonic()
        self.points[angle // 100] = (angle / 100.0, distance, now)
        self.last_point = now
        self.updated = time.time()
        self.received += 1

    def snapshot(self):
        with self.lock:
            now = time.monotonic()
            self.points = {key: point for key, point in self.points.items()
                           if now - point[2] < self.POINT_TTL}
            age = None if self.last_point is None else now - self.last_point
            return {
                "points": [[p[0], p[1]] for _, p in sorted(self.points.items())],
                "updated": self.updated,
                "online": age is not None and age < self.POINT_TTL,
                "age_ms": None if age is None else round(age * 1000),
                "received": self.received,
                "invalid": self.invalid,
                "rx_bytes": self.rx_bytes,
            }


RADAR = RadarBuffer()


def radar_reader():
    while True:
        data = SERIAL.read()
        if data is None:
            RADAR.reset_stream()
        elif data:
            RADAR.feed(data)
        time.sleep(0.01)


# --------------------------------------------------------------------------- #
# HTTP
# --------------------------------------------------------------------------- #
INDEX_HTML = r"""<!doctype html>
<html lang="zh">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>LintLizard 控制台</title>
<style>
  :root{color-scheme:dark}
  *{box-sizing:border-box}
  body{margin:0;font-family:-apple-system,"PingFang SC","Microsoft YaHei",sans-serif;
       background:#0f1216;color:#e6edf3;overflow:hidden}
  header{height:44px;display:flex;align-items:center;gap:12px;padding:0 14px;
         border-bottom:1px solid #21262d;font-size:14px}
  header b{color:#4ade80}
  header .sp{flex:1}
  header .stat{color:#8b949e;font-size:12px}
  main{height:calc(100vh - 44px);display:grid;grid-template-columns:minmax(280px,1fr) 1.2fr;
       gap:10px;padding:10px}
  .card{background:#161b22;border:1px solid #21262d;border-radius:10px;padding:10px;
        display:flex;flex-direction:column;min-height:0}
  .card h3{margin:0 0 8px;font-size:13px;font-weight:600;color:#8b949e;letter-spacing:.5px}
  #padWrap{flex:1;display:flex;align-items:center;justify-content:center}
  #pad{touch-action:none;cursor:crosshair;border-radius:50%;background:#0d1117;
       border:1px solid #30363d;max-width:100%}
  .right{display:grid;grid-template-rows:1.15fr 1fr;gap:10px;min-height:0}
  #cam{width:100%;height:100%;object-fit:contain;background:#000;border-radius:6px}
  #scan{width:100%;height:100%;background:#0d1117;border-radius:6px}
  .muted{color:#8b949e;font-size:12px}
  @media (max-width:820px){
    main{grid-template-columns:1fr;grid-template-rows:1.25fr 1fr}
    .right{grid-template-rows:1fr 1fr}
  }
</style>
</head>
<body>
<header>
  <b>LintLizard</b>
  <span class="stat" id="conn">连接中…</span>
  <span class="sp"></span>
  <span class="stat" id="vals">x=0 y=0</span>
</header>
<main>
  <section class="card">
    <h3>虚拟摇杆（上=前进 下=后退 左右=转向）</h3>
    <div id="padWrap"><canvas id="pad" width="320" height="320"></canvas></div>
    <div class="muted">按住拖动；松手自动回中</div>
  </section>
  <section class="right">
    <div class="card">
      <h3>摄像头</h3>
      <img id="cam" src="/video" alt="camera">
    </div>
    <div class="card">
      <h3>雷达点云 <span id="radarState">等待数据</span> · 圆环 1/2/3m · 上方为车头</h3>
      <canvas id="scan"></canvas>
    </div>
  </section>
</main>
<script>
const pad = document.getElementById('pad');
const ctx = pad.getContext('2d');
const vals = document.getElementById('vals');
const conn = document.getElementById('conn');
const R = pad.width / 2;
let knob = {x:0, y:0};
let dragging = false;
let lastSend = 0;

function drawPad(){
  ctx.clearRect(0,0,pad.width,pad.height);
  ctx.strokeStyle = '#30363d'; ctx.lineWidth = 2;
  ctx.beginPath(); ctx.arc(R,R,R-2,0,Math.PI*2); ctx.stroke();
  ctx.strokeStyle = '#21262d'; ctx.lineWidth = 1;
  ctx.beginPath(); ctx.moveTo(R,6); ctx.lineTo(R,pad.height-6);
  ctx.moveTo(6,R); ctx.lineTo(pad.width-6,R); ctx.stroke();
  const kx = R + knob.x * (R-24), ky = R + knob.y * (R-24);
  ctx.beginPath(); ctx.arc(kx,ky,22,0,Math.PI*2);
  ctx.fillStyle = dragging ? '#4ade80' : '#2ea043'; ctx.fill();
}

function toCmd(x, y){
  return {x: Math.round(x*100), y: Math.round(-y*100)};
}

function send(force){
  const now = performance.now();
  if (!force && now - lastSend < 50) return;
  lastSend = now;
  const c = toCmd(knob.x, knob.y);
  vals.textContent = 'x=' + c.x + ' y=' + c.y;
  fetch('/cmd?x=' + c.x + '&y=' + c.y).then(function(){
    conn.textContent = '已连接 · 串口正常';
    conn.style.color = '#4ade80';
  }).catch(function(){
    conn.textContent = '连接断开';
    conn.style.color = '#f85149';
  });
}

function pos(e){
  const r = pad.getBoundingClientRect();
  const p = e.touches ? e.touches[0] : e;
  let dx = (p.clientX - r.left) / r.width * 2 - 1;
  let dy = (p.clientY - r.top) / r.height * 2 - 1;
  const d = Math.hypot(dx, dy);
  if (d > 1){ dx /= d; dy /= d; }
  knob.x = dx; knob.y = dy;
}

function down(e){ dragging = true; pos(e); drawPad(); send(true); e.preventDefault(); }
function move(e){ if (dragging){ pos(e); drawPad(); send(false); e.preventDefault(); } }
function up(){ dragging = false; knob.x = 0; knob.y = 0; drawPad(); send(true); }

pad.addEventListener('mousedown', down);
window.addEventListener('mousemove', move);
window.addEventListener('mouseup', up);
pad.addEventListener('touchstart', down, {passive:false});
window.addEventListener('touchmove', move, {passive:false});
window.addEventListener('touchend', up);

const scan = document.getElementById('scan');
const sctx = scan.getContext('2d');
function drawScan(pts){
  const w = scan.clientWidth, h = scan.clientHeight;
  if (!w || !h) return;
  if (scan.width !== w || scan.height !== h){ scan.width = w; scan.height = h; }
  sctx.clearRect(0,0,w,h);
  const cx = w/2, cy = h/2, scale = Math.min(w,h) / 2 / 3.0;
  sctx.strokeStyle = '#21262d';
  for (let i=1;i<=3;i++){ sctx.beginPath(); sctx.arc(cx,cy,scale*i,0,Math.PI*2); sctx.stroke(); }
  sctx.beginPath(); sctx.moveTo(cx,cy-h/2); sctx.lineTo(cx,cy+h/2);
  sctx.moveTo(cx-w/2,cy); sctx.lineTo(cx+w/2,cy); sctx.stroke();
  sctx.fillStyle = '#4ade80';
  for (let i=0;i<pts.length;i++){
    const p = pts[i];
    const a = (p[0] - 90) * Math.PI/180;
    const d = p[1] / 1000.0;
    const x = cx + Math.cos(a) * d * scale;
    const y = cy + Math.sin(a) * d * scale;
    sctx.fillRect(x-1.5, y-1.5, 3, 3);
  }
}
async function pollScan(){
  try {
    const r = await fetch('/scan');
    const j = await r.json();
    drawScan(j.points || []);
    document.getElementById('radarState').textContent = j.online ? ('在线 · ' + j.points.length + ' 点') : '无新数据';
  } catch(e){
    drawScan([]);
    document.getElementById('radarState').textContent = '连接中断';
  }
  setTimeout(pollScan, 400);
}

drawPad();
pollScan();
setInterval(function(){ if (!dragging) send(true); }, 300);
</script>
</body>
</html>
"""


class Handler(BaseHTTPRequestHandler):
    server_version = "LintLizardPi/1.0"

    def log_message(self, fmt, *args):
        pass

    def _json(self, obj, code=200):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlparse(self.path)
        path = url.path
        query = parse_qs(url.query)

        if path in ("/", "/index.html"):
            body = INDEX_HTML.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        elif path == "/cmd":
            try:
                x = int(query.get("x", ["0"])[0])
                y = int(query.get("y", ["0"])[0])
            except ValueError:
                self._json({"ok": False, "error": "x/y 必须是整数"}, 400)
                return
            ok = SERIAL.send(x, y)
            self._json({"ok": ok, "x": x, "y": y, "tx": SERIAL.tx_count})

        elif path == "/status":
            radar = RADAR.snapshot()
            self._json({
                "uptime": round(time.time() - START_TIME, 1),
                "serial": SERIAL.status(),
                "camera": camera_available(),
                "radar_points": len(radar["points"]),
                "radar": {key: value for key, value in radar.items() if key != "points"},
            })

        elif path == "/scan":
            self._json(RADAR.snapshot())

        elif path == "/video":
            if not camera_available():
                self._json({"ok": False, "error": "没有检测到摄像头"}, 503)
                return
            self.send_response(200)
            self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            try:
                for part in mjpeg_frames():
                    self.wfile.write(part)
            except (BrokenPipeError, ConnectionResetError):
                pass

        else:
            self._json({"ok": False, "error": "not found"}, 404)


def main():
    global SERIAL
    SERIAL = SerialLink(SERIAL_PORT, SERIAL_BAUD)
    threading.Thread(target=SERIAL.watchdog, daemon=True).start()
    threading.Thread(target=radar_reader, daemon=True).start()
    server = ThreadingHTTPServer(("0.0.0.0", HTTP_PORT), Handler)
    print("[web] http://0.0.0.0:%d/  (Ctrl+C 退出)" % HTTP_PORT, flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
