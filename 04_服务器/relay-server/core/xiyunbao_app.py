"""Minimal WebSocket entry point for the Xiaoyunbao PCM relay."""

import asyncio
from urllib.parse import parse_qs, urlparse

import websockets
from websockets.exceptions import ConnectionClosed

from core.xiyunbao_relay import handle_xiyunbao_connection
from core.emotion_vision import get_vision
from core.vision_debug import (
    handle_vision_websocket,
    http_page_response,
    run_vision_debug,
    vision_http_path,
)


def _ws_path(websocket) -> str:
    request = getattr(websocket, "request", None)
    if request is not None:
        return getattr(request, "path", "") or ""
    return getattr(websocket, "path", "") or ""


async def process_request(*args):
    path = ""
    if len(args) == 2:
        first, second = args
        if hasattr(second, "path"):
            path = second.path or ""
        elif isinstance(first, str):
            path = first
        elif hasattr(first, "path"):
            path = first.path or ""
    if vision_http_path(path):
        return http_page_response()
    return None


async def run_xiyunbao_app(config):
    server_config = config.get("server", {})
    relay_config = config.get("xiyunbao", {})
    host = str(server_config.get("ip", "0.0.0.0"))
    port = int(server_config.get("port", 8000))
    expected_secret = str(relay_config.get("secret", ""))

    async def handle(websocket):
        parsed = urlparse(_ws_path(websocket))
        path = parsed.path.rstrip("/")
        if path in ("/vision/live", "/vision"):
            try:
                await handle_vision_websocket(websocket, expected_secret)
            except ConnectionClosed:
                pass
            return
        if path != "/xiyunbao/v1":
            await websocket.close(code=1008, reason="unsupported path")
            return
        supplied_secret = parse_qs(parsed.query).get("secret", [""])[0]
        if not expected_secret or supplied_secret != expected_secret:
            await websocket.send('{"type":"error","message":"secret校验失败"}')
            await websocket.close(code=1008, reason="invalid secret")
            return
        try:
            await handle_xiyunbao_connection(websocket, config)
        except ConnectionClosed as exc:
            print(f"小云宝连接关闭: code={exc.code}, reason={exc.reason or '无'}")
        except Exception as exc:
            print(f"小云宝连接处理失败: {exc}")
            try:
                await websocket.close(code=1011, reason="relay error")
            except ConnectionClosed:
                pass

    print(f"小云宝 Opus 中转服务已启动: ws://{host}:{port}/xiyunbao/v1/")
    print("ESP32 音频格式: 上行 PCM 16kHz；下行 Opus 24kHz/32kbps")
    vision = get_vision(config)
    print(f"视觉情绪: {vision.reason}  模型目录={vision.model_dir}")
    print("ESP32 视觉格式: 上行 JPEG QVGA；下行 vision.emotion")
    await run_vision_debug(host, expected_secret, ports=(8001, 8080, 80))
    serve_kwargs = dict(
        ping_interval=20,
        ping_timeout=10,
        max_size=8 * 1024 * 1024,
        process_request=process_request,
    )
    try:
        async with websockets.serve(handle, host, port, **serve_kwargs):
            print(f"网页实时画面: http://服务器:{port}/vision")
            await asyncio.Future()
    except TypeError:
        serve_kwargs.pop("process_request", None)
        async with websockets.serve(handle, host, port, **serve_kwargs):
            print("当前 websockets 版本不支持 HTTP 页面，请用 8001/8080 端口")
            await asyncio.Future()
