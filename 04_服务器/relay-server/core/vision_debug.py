"""In-memory vision debug page. Does not write photos to disk."""

from __future__ import annotations

import asyncio
import base64
import io
import json
import time
from urllib.parse import parse_qs, urlparse

from aiohttp import web

_LAST = {
    "raw_jpeg": b"",
    "used_jpeg": b"",
    "face_jpeg": b"",
    "meta": {},
    "updated": 0.0,
    "phase": "live",
}
_SECRET = ""
_WAITERS: set[asyncio.Queue] = set()

PAGE = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>小云宝视觉调试</title>
<style>
body { font-family: sans-serif; margin: 16px; background: #111; color: #eee; }
h1 { font-size: 20px; }
.row { display: flex; flex-wrap: wrap; gap: 12px; }
.card { background: #1c1c1c; padding: 12px; border-radius: 10px; min-width: 280px; flex: 1; }
img { max-width: 100%; background: #000; border-radius: 8px; }
.bar { height: 10px; background: #333; border-radius: 6px; margin: 4px 0 10px; }
.fill { height: 10px; background: #4ea1ff; border-radius: 6px; }
.ok { color: #7dffa3; }
.bad { color: #ff8a8a; }
input { padding: 6px 8px; width: 220px; }
</style>
</head>
<body>
<h1>小云宝视觉调试</h1>
<p>左边是摄像头实时画面。识别信息在下方同步刷新。屏幕上的伴侣界面不会被这页影响。</p>
<p>密钥 <input id="secret" type="password" placeholder="中转 secret"/>
<button onclick="save()">保存并刷新</button>
<span id="status">等待画面...</span></p>
<div class="row">
  <div class="card"><h3>实时画面</h3><img id="live" alt="live"/></div>
  <div class="card"><h3>识别用图</h3><img id="used" alt="used"/></div>
  <div class="card"><h3>裁出的脸</h3><img id="face" alt="face"/></div>
</div>
<div class="card" id="meta"></div>
<script>
let ws = null;
let lastLiveUrl = "";
function secret() {
  const q = new URLSearchParams(location.search).get("secret");
  return q || localStorage.getItem("xyb_secret") || document.getElementById("secret").value;
}
function save() {
  localStorage.setItem("xyb_secret", document.getElementById("secret").value);
  startLive();
}
function setLiveBlob(blob) {
  if (lastLiveUrl) URL.revokeObjectURL(lastLiveUrl);
  lastLiveUrl = URL.createObjectURL(blob);
  document.getElementById("live").src = lastLiveUrl;
}
function renderMeta(meta, age) {
  meta = meta || {};
  const cls = meta.ok ? "ok" : "bad";
  const ageText = age != null ? Number(age).toFixed(1) + " 秒前" : "无";
  let html = "<p class='" + cls + "'>更新: " + ageText +
    "　情绪: <b>" + (meta.emotion || "-") + "</b>" +
    "　置信度: " + (meta.confidence != null ? Number(meta.confidence).toFixed(3) : "-") +
    "　人脸: " + (meta.face ? "有" : "无") +
    "　方向: " + (meta.orientation || "-") +
    "　说明: " + (meta.message || "") + "</p>";
  const probs = meta.probs || {};
  for (const name of ["angry","fear","happy","neutral","surprise"]) {
    const v = Number(probs[name] || 0);
    html += name + " " + v.toFixed(3) +
      "<div class='bar'><div class='fill' style='width:" + (v * 100).toFixed(1) + "%'></div></div>";
  }
  document.getElementById("meta").innerHTML = html;
}
function useMjpeg() {
  return location.port !== "8000";
}
function startLive() {
  const s = secret();
  if (!s) { document.getElementById("status").textContent = "先填写密钥"; return; }
  if (ws) { try { ws.close(); } catch (e) {} ws = null; }
  if (useMjpeg()) {
    document.getElementById("live").src = "/vision/stream?secret=" + encodeURIComponent(s);
    document.getElementById("status").textContent = "实时视频流中";
    return;
  }
  const proto = location.protocol === "https:" ? "wss://" : "ws://";
  ws = new WebSocket(proto + location.host + "/vision/live?secret=" + encodeURIComponent(s));
  ws.binaryType = "blob";
  ws.onopen = () => document.getElementById("status").textContent = "实时视频流中";
  ws.onclose = () => document.getElementById("status").textContent = "画面断开，正在重连";
  ws.onerror = () => document.getElementById("status").textContent = "画面连接失败";
  ws.onmessage = (event) => {
    if (typeof event.data === "string") {
      try {
        const data = JSON.parse(event.data);
        renderMeta(data.meta || {}, data.age_s);
      } catch (e) {}
      return;
    }
    setLiveBlob(event.data);
  };
}
async function tickMeta() {
  const s = secret();
  if (!s) return;
  try {
    const t = Date.now();
    const res = await fetch("/vision/latest.json?secret=" + encodeURIComponent(s) + "&t=" + t);
    if (!res.ok) return;
    const data = await res.json();
    renderMeta(data.meta || {}, data.age_s);
    const q = "?secret=" + encodeURIComponent(s) + "&t=" + t;
    document.getElementById("used").src = "/vision/used.jpg" + q;
    document.getElementById("face").src = "/vision/face.jpg" + q;
  } catch (err) {}
}
startLive();
setInterval(tickMeta, 400);
tickMeta();
</script>
</body>
</html>
"""


def _placeholder_jpeg() -> bytes:
    from PIL import Image, ImageDraw, ImageFont

    image = Image.new("RGB", (320, 240), (18, 18, 18))
    draw = ImageDraw.Draw(image)
    text = "waiting for camera"
    try:
        font = ImageFont.load_default()
        draw.text((70, 110), text, fill=(180, 180, 180), font=font)
    except Exception:
        draw.text((70, 110), text, fill=(180, 180, 180))
    buf = io.BytesIO()
    image.save(buf, format="JPEG", quality=60)
    return buf.getvalue()


def _jpeg(image, quality=80) -> bytes:
    if image is None:
        return b""
    buf = io.BytesIO()
    image.convert("RGB").save(buf, format="JPEG", quality=quality)
    return buf.getvalue()


def _notify_waiters(jpeg: bytes) -> None:
    if not jpeg:
        return
    stale = []
    for queue in list(_WAITERS):
        try:
            if queue.full():
                try:
                    queue.get_nowait()
                except Exception:
                    pass
            queue.put_nowait(jpeg)
        except Exception:
            stale.append(queue)
    for queue in stale:
        _WAITERS.discard(queue)


def remember_preview(raw_jpeg: bytes) -> None:
    if not raw_jpeg:
        return
    _LAST["raw_jpeg"] = raw_jpeg
    _LAST["used_jpeg"] = raw_jpeg
    _LAST["updated"] = time.time()
    _LAST["phase"] = "live"
    meta = dict(_LAST.get("meta") or {})
    meta["phase"] = "live"
    _LAST["meta"] = meta
    _notify_waiters(raw_jpeg)


def remember_vision(raw_jpeg: bytes, result: dict) -> None:
    meta = {key: value for key, value in result.items() if not str(key).startswith("_")}
    meta["phase"] = "analyze"
    _LAST["raw_jpeg"] = raw_jpeg or _LAST.get("raw_jpeg") or b""
    used = result.get("_debug_used")
    face = result.get("_debug_face")
    _LAST["used_jpeg"] = _jpeg(used) if used is not None else (_LAST["raw_jpeg"] or b"")
    _LAST["face_jpeg"] = _jpeg(face) if face is not None else b""
    _LAST["meta"] = meta
    _LAST["updated"] = time.time()
    _LAST["phase"] = "analyze"
    _notify_waiters(_LAST["raw_jpeg"])


def latest_payload() -> dict:
    age = (time.time() - _LAST["updated"]) if _LAST["updated"] else None
    return {
        "ok": True,
        "age_s": age,
        "phase": _LAST.get("phase") or "live",
        "meta": _LAST["meta"],
    }


def _authorized(request: web.Request) -> bool:
    secret = parse_qs(urlparse(str(request.rel_url)).query).get("secret", [""])[0]
    return bool(_SECRET) and secret == _SECRET


def authorized_secret(secret: str) -> bool:
    return bool(_SECRET) and secret == _SECRET


async def page(_request):
    return web.Response(text=PAGE, content_type="text/html")


async def latest_json(request):
    if not _authorized(request):
        return web.json_response({"ok": False, "message": "secret校验失败"}, status=403)
    return web.json_response(latest_payload())


async def snapshot(request):
    if not _authorized(request):
        return web.json_response({"ok": False, "message": "secret校验失败"}, status=403)
    data = latest_payload()
    data["raw_jpeg_b64"] = base64.b64encode(_LAST.get("raw_jpeg") or b"").decode("ascii")
    data["used_jpeg_b64"] = base64.b64encode(_LAST.get("used_jpeg") or b"").decode("ascii")
    data["face_jpeg_b64"] = base64.b64encode(_LAST.get("face_jpeg") or b"").decode("ascii")
    return web.json_response(data)


async def _image(request, key: str):
    if not _authorized(request):
        return web.Response(status=403, text="secret校验失败")
    body = _LAST.get(key) or b""
    if not body:
        return web.Response(status=404, text="还没有画面")
    return web.Response(body=body, content_type="image/jpeg")


async def raw_jpg(request):
    return await _image(request, "raw_jpeg")


async def used_jpg(request):
    return await _image(request, "used_jpeg")


async def face_jpg(request):
    return await _image(request, "face_jpeg")


async def stream(request):
    if not _authorized(request):
        return web.Response(status=403, text="secret校验失败")
    boundary = "frame"
    response = web.StreamResponse(
        status=200,
        headers={
            "Content-Type": f"multipart/x-mixed-replace; boundary={boundary}",
            "Cache-Control": "no-cache, no-store, must-revalidate",
            "Pragma": "no-cache",
            "Connection": "keep-alive",
        },
    )
    await response.prepare(request)
    queue: asyncio.Queue = asyncio.Queue(maxsize=2)
    _WAITERS.add(queue)
    jpeg = _LAST.get("raw_jpeg") or _placeholder_jpeg()
    last_sent = b""
    try:
        while True:
            if jpeg and jpeg != last_sent:
                header = (
                    f"--{boundary}\r\n"
                    "Content-Type: image/jpeg\r\n"
                    f"Content-Length: {len(jpeg)}\r\n\r\n"
                ).encode("ascii")
                try:
                    await asyncio.wait_for(response.write(header + jpeg + b"\r\n"), timeout=0.25)
                    last_sent = jpeg
                except Exception:
                    break
            try:
                jpeg = await asyncio.wait_for(queue.get(), timeout=0.2)
            except asyncio.TimeoutError:
                jpeg = _LAST.get("raw_jpeg") or jpeg
    except (asyncio.CancelledError, ConnectionResetError, BrokenPipeError):
        pass
    finally:
        _WAITERS.discard(queue)
    return response


async def handle_vision_websocket(websocket, expected_secret: str) -> None:
    raw = ""
    request = getattr(websocket, "request", None)
    if request is not None:
        raw = getattr(request, "path", "") or ""
    if not raw:
        raw = getattr(websocket, "path", "") or ""
    parsed = urlparse(raw)
    secret = parse_qs(parsed.query).get("secret", [""])[0]
    if not authorized_secret(secret) and secret != expected_secret:
        await websocket.close(code=1008, reason="invalid secret")
        return
    queue: asyncio.Queue = asyncio.Queue(maxsize=2)
    _WAITERS.add(queue)
    jpeg = _LAST.get("raw_jpeg") or b""
    try:
        if jpeg:
            await websocket.send(jpeg)
        await websocket.send(json.dumps(latest_payload(), ensure_ascii=False))
        while True:
            try:
                jpeg = await asyncio.wait_for(queue.get(), timeout=0.5)
                await websocket.send(jpeg)
            except asyncio.TimeoutError:
                await websocket.send(json.dumps(latest_payload(), ensure_ascii=False))
    finally:
        _WAITERS.discard(queue)


def http_page_response():
    body = PAGE.encode("utf-8")
    headers = [
        ("Content-Type", "text/html; charset=utf-8"),
        ("Cache-Control", "no-cache"),
        ("Content-Length", str(len(body))),
    ]
    try:
        from websockets.datastructures import Headers
        from websockets.http11 import Response

        return Response(200, "OK", Headers(headers), body)
    except Exception:
        from http import HTTPStatus

        return HTTPStatus.OK, headers, body


def vision_http_path(path: str) -> bool:
    parsed = urlparse(path or "")
    return parsed.path.rstrip("/") == "/vision"


def make_vision_app() -> web.Application:
    app = web.Application()
    app.router.add_get("/vision", page)
    app.router.add_get("/vision/", page)
    app.router.add_get("/vision/latest.json", latest_json)
    app.router.add_get("/vision/snapshot", snapshot)
    app.router.add_get("/vision/raw.jpg", raw_jpg)
    app.router.add_get("/vision/used.jpg", used_jpg)
    app.router.add_get("/vision/face.jpg", face_jpg)
    app.router.add_get("/vision/stream", stream)
    return app


async def run_vision_debug(host: str, secret: str, ports=None):
    global _SECRET
    _SECRET = str(secret or "")
    if ports is None:
        ports = (8001, 8080, 80)
    runner = web.AppRunner(make_vision_app())
    await runner.setup()
    opened = []
    for port in ports:
        try:
            site = web.TCPSite(runner, host or "0.0.0.0", int(port))
            await site.start()
            opened.append(int(port))
            print(f"视觉调试页: http://127.0.0.1:{port}/vision")
        except OSError as exc:
            print(f"视觉调试端口 {port} 无法监听: {exc}")
    if not opened:
        print("视觉调试页没有成功监听任何端口")
    else:
        print("本机浏览可 SSH 隧道: ssh -L 8001:127.0.0.1:8001 USER@服务器")
        print("公网可试: http://服务器:8000/vision  （走已开放的中转端口）")
