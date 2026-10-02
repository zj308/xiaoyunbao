#!/usr/bin/env python3
"""Synthesize the English-mode greeting with the project's CosyVoice settings."""

from __future__ import annotations

import asyncio
import json
import re
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SECRETS = ROOT / "secrets.local.h"
OUTPUT = ROOT / "data" / "ui" / "english_ready.pcm"
TEXT = "英文模式准备好啦，Feel free to talk with me in English."
HAPPY_RATE = 0.92
HAPPY_PITCH = 1.05


def read_macro(name: str, default: str = "") -> str:
    text = SECRETS.read_text(encoding="utf-8")
    match = re.search(rf'^\s*#define\s+{re.escape(name)}\s+"([^"]*)"', text, re.M)
    return match.group(1) if match else default


async def synthesize() -> bytes:
    try:
        import websockets
    except ImportError:
        sys.stderr.write("缺少 websockets，正在安装\n")
        import subprocess

        subprocess.check_call([sys.executable, "-m", "pip", "install", "websockets"])
        import websockets

    api_key = read_macro("XIYUNBAO_DASHSCOPE_API_KEY")
    workspace = read_macro("XIYUNBAO_WORKSPACE_ID")
    host = read_macro("XIYUNBAO_TTS_HOST") or f"{workspace}.cn-beijing.maas.aliyuncs.com"
    path = read_macro("XIYUNBAO_TTS_PATH") or "/api-ws/v1/inference"
    model = read_macro("XIYUNBAO_TTS_MODEL") or "qwen-audio-3.0-tts-flash"
    voice = read_macro("XIYUNBAO_COSYVOICE_VOICE_ID")
    if not api_key or not voice:
        raise SystemExit("secrets.local.h 缺少 TTS Key 或音色")

    headers = {
        "Authorization": f"Bearer {api_key}",
        "Origin": "https://dashscope.aliyuncs.com",
    }
    if workspace:
        headers["X-DashScope-WorkSpace"] = workspace

    uri = f"wss://{host}{path}"
    task_id = uuid.uuid4().hex
    pcm = bytearray()
    print(f"连接 {host}{path}")
    print(f"音色 {voice}，情绪 happy，rate={HAPPY_RATE}, pitch={HAPPY_PITCH}")

    async with websockets.connect(
        uri,
        additional_headers=headers,
        open_timeout=15,
        close_timeout=5,
        max_size=8 * 1024 * 1024,
    ) as ws:
        await ws.send(
            json.dumps(
                {
                    "header": {"action": "run-task", "task_id": task_id, "streaming": "duplex"},
                    "payload": {
                        "task_group": "audio",
                        "task": "tts",
                        "function": "SpeechSynthesizer",
                        "model": model,
                        "parameters": {
                            "text_type": "PlainText",
                            "voice": voice,
                            "format": "pcm",
                            "sample_rate": 24000,
                            "volume": 50,
                            "rate": HAPPY_RATE,
                            "pitch": HAPPY_PITCH,
                            "enable_ssml": False,
                        },
                        "input": {},
                    },
                },
                ensure_ascii=False,
                separators=(",", ":"),
            )
        )
        started = False
        finished = False
        while not finished:
            message = await asyncio.wait_for(ws.recv(), timeout=20)
            if isinstance(message, (bytes, bytearray)):
                pcm.extend(message)
                continue
            data = json.loads(message)
            header = data.get("header") or {}
            event = header.get("event")
            if event == "task-started":
                started = True
                await ws.send(
                    json.dumps(
                        {
                            "header": {
                                "action": "continue-task",
                                "task_id": task_id,
                                "streaming": "duplex",
                            },
                            "payload": {"input": {"text": TEXT}},
                        },
                        ensure_ascii=False,
                        separators=(",", ":"),
                    )
                )
                await ws.send(
                    json.dumps(
                        {
                            "header": {
                                "action": "finish-task",
                                "task_id": task_id,
                                "streaming": "duplex",
                            },
                            "payload": {"input": {}},
                        },
                        separators=(",", ":"),
                    )
                )
            elif event == "task-finished":
                finished = True
            elif event == "task-failed":
                raise RuntimeError(header.get("error_message") or json.dumps(data, ensure_ascii=False))
            elif event:
                print(f"TTS 事件: {event}")
        if not started:
            raise RuntimeError("TTS 未进入 task-started")

    if len(pcm) < 24000:
        raise RuntimeError(f"PCM 过短: {len(pcm)} bytes")
    return bytes(pcm)


def main() -> None:
    pcm = asyncio.run(synthesize())
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(pcm)
    seconds = len(pcm) / (24000 * 2)
    print(f"已写入 {OUTPUT} ，{len(pcm)} 字节，约 {seconds:.2f} 秒")


if __name__ == "__main__":
    main()
