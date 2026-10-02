"""Audio relay for the SmartCompanionS3 firmware.

The device sends 16 kHz mono PCM and receives compressed 24 kHz mono Opus.
Cloud credentials stay on the server; control messages are UTF-8 JSON frames.
"""

import ast
import asyncio
import json
import os
import re
import struct
import time
import uuid
from collections import deque
from pathlib import Path

import aiohttp
import opuslib_next
import websockets
from loguru import logger

from core.emotion_vision import get_vision
from core.vision_debug import remember_preview, remember_vision


TAG = __name__
LOGGER = logger
VISION_CLUE_TTL_S = 90.0
KNOWN_TAG_RE = re.compile(r"^\s*(emotion|情绪|memory[-_]fact)\s*[:：]\s*(.*?)\s*$", re.I | re.S)
INLINE_EMOTION_RE = re.compile(
    r"[【\[\[]\s*(?:emotion|情绪)\s*[:：]\s*"
    r"(happy|sad|neutral|shy|surprise|surprised|angry|fear)\s*[】\]］]",
    re.I,
)
BARE_EMOTION_RE = re.compile(
    r"[【\[\[]\s*(happy|sad|neutral|shy|surprise|surprised|angry|fear)\s*[】\]］]",
    re.I,
)
BARE_EMOTION_BODY_RE = re.compile(
    r"^(happy|sad|neutral|shy|surprise|surprised|angry|fear)$",
    re.I,
)
LINE_EMOTION_RE = re.compile(
    r"^\s*(?:the\s+)?(?:emotion(?:\s+tag)?|mood|feeling|情绪)\s*[:：]?\s*"
    r"(?:is\s+)?(happy|sad|neutral|shy|surprise|surprised|angry|fear)\s*[.!,]?\s*$",
    re.I,
)
SPOKEN_EMOTION_PREFIX_RE = re.compile(
    r"^\s*(?:the\s+)?(?:emotion(?:\s+tag)?|mood|feeling)\s*(?:is|:|：)\s*"
    r"(happy|sad|neutral|shy|surprise|surprised|angry|fear)\s*[.!,]?\s*",
    re.I,
)
NOISE_ASR_RE = re.compile(
    r"^(uh+|um+|hmm+|mm+|ah+|oh+|er+|eh+|ha+|huh+|mhm+|the|a|an|uhhuh|啊+|嗯+|哦+|呃+|唉+|额+)$",
    re.I,
)
SENTENCE_ENDINGS = set("。！？!?；;\n")
SOFT_ENDINGS = set("，,、")
RELAY_OPUS_SAMPLE_RATE = 24000
RELAY_OPUS_CHANNELS = 1
RELAY_OPUS_FRAME_MS = 40
RELAY_OPUS_FRAME_SAMPLES = RELAY_OPUS_SAMPLE_RATE * RELAY_OPUS_FRAME_MS // 1000
RELAY_OPUS_FRAME_BYTES = RELAY_OPUS_FRAME_SAMPLES * 2
RELAY_OPUS_BITRATE = 32000
RELAY_OPUS_MAGIC = b"XYOP"
RELAY_OPUS_VERSION = 1
RELAY_OPUS_HEADER = struct.Struct("<4sBBHHH")

ENGLISH_COACH_PROMPT = (
    "You are XiaoYunBao, a warm CET-4 speaking companion for a Chinese university student. "
    "Treat this as one continuous conversation: follow the last turn, do not restart, "
    "do not re-introduce yourself unless asked. "
    "The student may speak English, Chinese, or a mix of both. Always understand their meaning. "
    "Your spoken reply MUST be entirely in natural, clear English. Never reply in Chinese, "
    "even if the student spoke only Chinese. If they used Chinese, briefly restate their idea "
    "in simple English, help them say it, then continue. "
    "Keep replies short for speech: 2-4 sentences. "
    "Each turn, do at most ONE of these coaching moves: "
    "(1) recast a mistake into a natural sentence they can repeat; "
    "(2) offer one useful CET-4 word or chunk with a tiny example; "
    "(3) ask one follow-up that keeps the same topic. "
    "If a visual-emotion clue is given, use it only to choose the silent header. "
    "Do not mention the clue, the word emotion, or any tag name out loud. "
    "Output format is mandatory and silent: the first line MUST be exactly 【emotion:tag】 "
    "using those Chinese brackets, where tag is one of happy/sad/neutral/shy/surprise. "
    "That first line is a machine control header. Never speak, spell, translate, or explain it. "
    "Never write a bare tag such as 【neutral】 or [happy]. Never say phrases like "
    "'emotion happy', 'neutral', or 'the emotion is sad'. "
    "Always keep a normal space between English words. "
    "The spoken English starts on the next line and must be ordinary conversation only. "
    "Do not use Chinese example replies. Follow the same safety rules as a child-friendly companion."
)

CONTINUITY_SUFFIX = (
    "\n把每一轮都当成同一段聊天的下一拍。"
    "必须承接上一轮用户说的事和你自己刚回过的话，"
    "禁止突然换题、禁止重复自我介绍、禁止装作第一次见面。"
    "用户纠正你或提起刚才的细节时，先认账再继续。\n"
)


def _macro(path: Path, name: str, default: str = "") -> str:
    if not path.exists():
        return default
    text = path.read_text(encoding="utf-8", errors="ignore")
    # 兼容 Arduino 预处理器允许的 `MACRO"value"` 写法；旧配置曾遗漏分隔空格。
    match = re.search(rf'^\s*#define\s+{re.escape(name)}\s*"([^"]*)"', text, re.M)
    return match.group(1) if match else default


def _firmware_prompt(path: Path) -> str:
    if not path.exists():
        raise FileNotFoundError(f"找不到固件文件，无法读取原提示词: {path}")
    text = path.read_text(encoding="utf-8", errors="strict")
    match = re.search(
        r"const\s+char\s+QWEN_SYSTEM_PROMPT\[\]\s*=\s*(.*?);",
        text,
        re.S,
    )
    if not match:
        raise ValueError("固件中未找到 QWEN_SYSTEM_PROMPT")
    parts = re.findall(r'"(?:\\.|[^"\\])*"', match.group(1))
    return "".join(ast.literal_eval(part) for part in parts)


def _last_string(value, key: str) -> str:
    result = ""
    if isinstance(value, dict):
        for item_key, item_value in value.items():
            if item_key == key and isinstance(item_value, str) and item_value:
                result = item_value
            nested = _last_string(item_value, key)
            if nested:
                result = nested
    elif isinstance(value, list):
        for item in value:
            nested = _last_string(item, key)
            if nested:
                result = nested
    return result


def _has_true(value, key: str) -> bool:
    if isinstance(value, dict):
        return any(
            (item_key == key and item_value is True) or _has_true(item_value, key)
            for item_key, item_value in value.items()
        )
    if isinstance(value, list):
        return any(_has_true(item, key) for item in value)
    return False


def _normalize_emotion(candidate: str) -> str:
    value = (candidate or "").strip().lower()
    if value == "surprised":
        return "surprise"
    if value in {"fear", "angry"}:
        return "sad"
    if value in {"happy", "sad", "neutral", "shy", "surprise"}:
        return value
    return ""


def sanitize_spoken_text(text: str, tag_filter=None) -> str:
    """Drop silent emotion headers so CosyVoice never reads them aloud."""
    if not text:
        return ""

    def remember(label: str):
        if tag_filter is None:
            return
        emotion = _normalize_emotion(label)
        if emotion:
            tag_filter.emotion = emotion

    def inline_sub(match):
        remember(match.group(1))
        return " "

    cleaned = INLINE_EMOTION_RE.sub(inline_sub, text)
    cleaned = BARE_EMOTION_RE.sub(inline_sub, cleaned)
    kept = []
    for index, line in enumerate(cleaned.splitlines()):
        line_match = LINE_EMOTION_RE.match(line.strip())
        if line_match:
            remember(line_match.group(1))
            continue
        if index == 0:
            prefix = SPOKEN_EMOTION_PREFIX_RE.match(line)
            if prefix:
                remember(prefix.group(1))
                rest = line[prefix.end():].strip()
                if rest:
                    kept.append(rest)
                continue
        kept.append(line)
    cleaned = "\n".join(kept)
    cleaned = re.sub(r"^[\]】]+", "", cleaned)
    cleaned = re.sub(r"\s{2,}", " ", cleaned)
    return cleaned.strip()


def _is_noise_transcript(text: str, language: str) -> bool:
    stripped = (text or "").strip()
    if not stripped:
        return True
    letters = re.sub(r"[^A-Za-z\u4e00-\u9fff]+", "", stripped)
    if not letters:
        return True
    compact = re.sub(r"[\s.!,?，。！？]+", "", stripped)
    if NOISE_ASR_RE.match(compact):
        return True
    if language == "en" and len(letters) <= 1:
        return True
    return False


class TagFilter:
    """Incrementally removes only the two control-tag families we own."""

    def __init__(self):
        self.pending = ""
        self.emotion = "neutral"
        self.emotion_sent = False

    def remember_emotion(self, candidate: str):
        emotion = _normalize_emotion(candidate)
        if emotion:
            self.emotion = emotion

    def feed(self, chunk: str, final: bool = False):
        self.pending += chunk
        output = []
        while self.pending:
            marker_full = self.pending.find("【")
            lowered = self.pending.lower()
            marker_ascii = -1
            for token in ("[emotion", "[情绪"):
                found = lowered.find(token)
                if found >= 0 and (marker_ascii < 0 or found < marker_ascii):
                    marker_ascii = found
            markers = [item for item in (marker_full, marker_ascii) if item >= 0]
            if not markers:
                hold_at = self.pending.rfind("[")
                if not final and hold_at >= 0:
                    tail = self.pending[hold_at:].lower()
                    if "[emotion".startswith(tail) or "[情绪".startswith(tail):
                        if hold_at > 0:
                            output.append(self.pending[:hold_at])
                            self.pending = self.pending[hold_at:]
                        break
                output.append(self.pending)
                self.pending = ""
                break
            marker = min(markers)
            opener = self.pending[marker]
            closer = "】" if opener == "【" else "]"
            if marker > 0:
                output.append(self.pending[:marker])
                self.pending = self.pending[marker:]
                continue
            end = self.pending.find(closer)
            if end < 0:
                if final:
                    output.append(self.pending)
                    self.pending = ""
                break
            tag_body = self.pending[1:end]
            tag_match = KNOWN_TAG_RE.match(tag_body)
            bare_match = BARE_EMOTION_BODY_RE.match(tag_body.strip())
            if tag_match:
                kind = tag_match.group(1).lower().replace("_", "-")
                if kind in {"emotion", "情绪"}:
                    self.remember_emotion(tag_match.group(2))
                self.pending = self.pending[end + 1:]
                continue
            if bare_match:
                self.remember_emotion(bare_match.group(1))
                self.pending = self.pending[end + 1:]
                continue
            output.append(self.pending[:end + 1])
            self.pending = self.pending[end + 1:]
        return sanitize_spoken_text("".join(output), self)


class SentenceSplitter:
    def __init__(self, language: str = "zh"):
        self.pending = ""
        self.language = language
        self.started = False

    def feed(self, text: str, final: bool = False):
        self.pending += text
        sentences = []
        start = 0
        for index, char in enumerate(self.pending):
            early_clause = (
                not self.started
                and char in SOFT_ENDINGS
                and index - start >= (10 if self.language == "en" else 6)
            )
            if char in SENTENCE_ENDINGS or early_clause:
                sentence = self.pending[start:index + 1].strip()
                if sentence:
                    sentences.append(sentence)
                    self.started = True
                start = index + 1
        self.pending = self.pending[start:]
        if not self.started:
            limit = 28 if self.language == "en" else 12
            if len(self.pending) >= limit:
                cut = self.pending.rfind(" ") if self.language == "en" else len(self.pending)
                if cut < 8:
                    cut = len(self.pending)
                piece = self.pending[:cut].strip()
                if piece:
                    sentences.append(piece)
                    self.started = True
                    self.pending = self.pending[cut:].lstrip()
        if final and self.pending.strip():
            sentences.append(self.pending.strip())
            self.pending = ""
        return sentences


class XiaoyunbaoRelay:
    def __init__(self, websocket, config):
        relay = config.get("xiyunbao", {})
        self.websocket = websocket
        self.firmware_path = Path(relay.get("firmware_path", r"C:\xiyunbao\SmartCompanionS3\SmartCompanionS3.ino"))
        self.secrets_path = Path(relay.get("secrets_path", str(self.firmware_path.with_name("secrets.local.h"))))
        self.api_key = os.getenv("XIYUNBAO_DASHSCOPE_API_KEY") or _macro(self.secrets_path, "XIYUNBAO_DASHSCOPE_API_KEY")
        self.workspace = os.getenv("XIYUNBAO_WORKSPACE_ID") or _macro(self.secrets_path, "XIYUNBAO_WORKSPACE_ID")
        workspace_host = f"{self.workspace}.cn-beijing.maas.aliyuncs.com" if self.workspace else "dashscope.aliyuncs.com"
        self.asr_host = relay.get("cloud_ws_host") or _macro(self.secrets_path, "XIYUNBAO_ASR_HOST", workspace_host)
        self.asr_path = _macro(self.secrets_path, "XIYUNBAO_ASR_PATH", "/api-ws/v1/inference/")
        self.asr_model = relay.get("asr_model") or _macro(self.secrets_path, "XIYUNBAO_ASR_MODEL", "qwen-audio-3.0-asr-flash-streaming")
        self.qwen_model = relay.get("qwen_model") or _macro(self.secrets_path, "XIYUNBAO_QWEN_MODEL", "qwen-turbo")
        self.tts_model = relay.get("tts_model") or _macro(self.secrets_path, "XIYUNBAO_TTS_MODEL", "qwen-audio-3.0-tts-flash")
        self.tts_voice = relay.get("tts_voice") or _macro(self.secrets_path, "XIYUNBAO_COSYVOICE_VOICE_ID", "longhongxiuyi")
        self.max_tokens = int(relay.get("max_tokens", 96))
        self.prompt = _firmware_prompt(self.firmware_path) + CONTINUITY_SUFFIX
        self.language = "zh"
        self.history = deque(maxlen=int(relay.get("history_messages", 20)))
        self.audio_queue = None
        self.asr_task = None
        self.turn_task = None
        self.tts_ws = None
        self.tts_lock = asyncio.Lock()
        self.prepared_ws = None
        self.prepared_task_id = None
        self.prepare_task = None
        self.send_lock = asyncio.Lock()
        self.turn_lock = asyncio.Lock()
        self.pending_vision_jpeg = False
        self.pending_vision_mode = "analyze"
        self.vision_task = None
        self.last_vision = {}
        self.last_infer_at = 0.0
        self.vision = get_vision(config)
        LOGGER.info("视觉情绪模型: {} dir={}", self.vision.reason, self.vision.model_dir)
        if not self.api_key:
            raise ValueError("未找到百炼 API Key，请设置环境变量或保留固件 secrets.local.h")

    async def send_json(self, message_type: str, **payload):
        body = {"type": message_type, **payload}
        async with self.send_lock:
            await self.websocket.send(json.dumps(body, ensure_ascii=False, separators=(",", ":")))

    async def send_opus_packet(self, encoded: bytes, sequence: int, valid_samples: int, flags: int = 0):
        if not encoded or len(encoded) > 0xFFFF:
            raise ValueError("Opus 负载长度无效")
        packet = RELAY_OPUS_HEADER.pack(
            RELAY_OPUS_MAGIC,
            RELAY_OPUS_VERSION,
            flags,
            sequence & 0xFFFF,
            valid_samples,
            len(encoded),
        ) + encoded
        async with self.send_lock:
            await self.websocket.send(packet)
            await asyncio.sleep(0)
        return len(packet)

    async def _cancel_task(self, attribute: str):
        """Cancel one relay task and wait until its cleanup has completed."""
        task = getattr(self, attribute, None)
        if task is None:
            return
        if not task.done():
            task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass
        except Exception as exc:
            LOGGER.bind(tag=TAG).warning("relay task {} stopped with error: {}", attribute, exc)
        finally:
            if getattr(self, attribute, None) is task:
                setattr(self, attribute, None)

    async def _close_prepared_asr(self):
        await self._cancel_task("prepare_task")
        ws = self.prepared_ws
        self.prepared_ws = None
        self.prepared_task_id = None
        if ws is not None:
            try:
                await ws.close()
            except Exception:
                pass

    async def run(self):
        await self.send_json("hello", protocol=2, input="pcm_s16le_16000_mono", output="opus_24000_mono")
        try:
            async for message in self.websocket:
                if isinstance(message, bytes):
                    if self.pending_vision_jpeg:
                        self.pending_vision_jpeg = False
                        mode = self.pending_vision_mode or "analyze"
                        self.pending_vision_mode = "analyze"
                        await self._accept_vision_jpeg(message, mode)
                        continue
                    if self.audio_queue is not None:
                        await self.audio_queue.put(message)
                    continue
                try:
                    data = json.loads(message)
                except json.JSONDecodeError:
                    await self.send_json("error", message="控制消息不是合法 JSON")
                    continue
                kind = data.get("type")
                if kind == "listen.prepare":
                    language = str(data.get("language", self.language)).lower()
                    if language in ("en", "zh"):
                        self.language = language
                    self.prepare_listening()
                elif kind == "listen.start":
                    language = str(data.get("language", self.language)).lower()
                    if language in ("en", "zh"):
                        self.language = language
                    await self.start_listening()
                elif kind == "mode.set":
                    language = str(data.get("language", "zh")).lower()
                    self.language = "en" if language == "en" else "zh"
                    self.history.clear()
                    await self._cancel_task("asr_task")
                    if self.audio_queue is not None:
                        try:
                            self.audio_queue.put_nowait(None)
                        except Exception:
                            pass
                        self.audio_queue = None
                    await self._cancel_task("turn_task")
                    await self._close_prepared_asr()
                    if self.tts_ws is not None:
                        tts_ws = self.tts_ws
                        self.tts_ws = None
                        try:
                            await tts_ws.close()
                        except Exception:
                            pass
                    await self.send_json("mode.changed", language=self.language)
                    self.prepare_listening()
                elif kind == "listen.stop":
                    if self.audio_queue is not None:
                        await self.audio_queue.put(None)
                elif kind == "say":
                    text = str(data.get("text", "")).strip()
                    if text and (self.turn_task is None or self.turn_task.done()):
                        self.turn_task = asyncio.create_task(self.speak_text(text, str(data.get("emotion", "neutral"))))
                elif kind == "ping":
                    await self.send_json("pong")
                elif kind == "vision.frame":
                    self.pending_vision_jpeg = True
                    mode = str(data.get("mode") or "analyze").strip().lower()
                    self.pending_vision_mode = "preview" if mode == "preview" else "analyze"
        finally:
            tasks = [task for task in (self.asr_task, self.turn_task, self.vision_task, self.prepare_task)
                     if task is not None and not task.done()]
            for task in tasks:
                task.cancel()
            if tasks:
                await asyncio.gather(*tasks, return_exceptions=True)
            await self._close_prepared_asr()
            if self.tts_ws:
                await self.tts_ws.close()
                self.tts_ws = None

    def prepare_listening(self):
        if self.asr_task and not self.asr_task.done():
            return
        if self.prepare_task and not self.prepare_task.done():
            return
        if self.prepared_ws is not None:
            return
        self.prepare_task = asyncio.create_task(self._prepare_asr())

    async def _prepare_asr(self):
        ws = None
        try:
            ws = await websockets.connect(
                f"wss://{self.asr_host}{self.asr_path}",
                additional_headers=self.cloud_headers(),
                open_timeout=10,
                close_timeout=3,
                ping_interval=20,
                ping_timeout=10,
                max_size=2 * 1024 * 1024,
            )
            task_id = uuid.uuid4().hex
            asr_parameters = {"sample_rate": 16000, "format": "pcm"}
            if self.language == "en":
                asr_parameters["language_hints"] = ["en", "zh"]
            else:
                asr_parameters["language_hints"] = ["zh", "en"]
            await ws.send(json.dumps({
                "header": {"action": "run-task", "task_id": task_id, "streaming": "duplex"},
                "payload": {"task_group": "audio", "task": "asr", "function": "recognition",
                            "model": self.asr_model, "parameters": asr_parameters, "input": {}},
            }, separators=(",", ":")))
            while True:
                message = await asyncio.wait_for(ws.recv(), timeout=12)
                if not isinstance(message, str):
                    continue
                event = json.loads(message).get("header", {}).get("event", "")
                if event == "task-started":
                    self.prepared_ws = ws
                    self.prepared_task_id = task_id
                    LOGGER.info("下一轮识别已提前就绪")
                    return
                if event == "task-failed":
                    raise RuntimeError("ASR prepare failed")
        except asyncio.CancelledError:
            if ws is not None and ws is not self.prepared_ws:
                try:
                    await ws.close()
                except Exception:
                    pass
            raise
        except Exception as exc:
            LOGGER.bind(tag=TAG).warning("提前准备识别失败: {}", exc)
            if ws is not None and ws is not self.prepared_ws:
                await ws.close()
            self.prepared_ws = None
            self.prepared_task_id = None

    async def start_listening(self):
        await self._cancel_task("turn_task")
        await self._cancel_task("asr_task")
        if self.audio_queue is not None:
            try:
                self.audio_queue.put_nowait(None)
            except Exception:
                pass
        prepared_ws = None
        prepared_id = None
        if self.prepare_task is not None:
            try:
                await asyncio.wait_for(self.prepare_task, timeout=8)
            except Exception:
                LOGGER.bind(tag=TAG).warning("等待提前识别超时，改为当场连接")
            self.prepare_task = None
        if self.prepared_ws is not None:
            prepared_ws = self.prepared_ws
            prepared_id = self.prepared_task_id
            self.prepared_ws = None
            self.prepared_task_id = None
        self.audio_queue = asyncio.Queue(maxsize=128)
        self.asr_task = asyncio.create_task(self._asr_round(prepared_ws, prepared_id))

    def cloud_headers(self):
        headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Origin": "https://dashscope.aliyuncs.com",
        }
        if self.workspace:
            headers["X-DashScope-WorkSpace"] = self.workspace
        return headers

    async def _asr_round(self, prepared_ws=None, prepared_id=None):
        final_text = ""
        task_id = prepared_id or uuid.uuid4().hex
        ws = prepared_ws
        sender = None
        answer_started = False
        try:
            if ws is not None:
                await self.send_json("listen.started")
            else:
                ws = await websockets.connect(
                    f"wss://{self.asr_host}{self.asr_path}",
                    additional_headers=self.cloud_headers(),
                    open_timeout=10,
                    close_timeout=3,
                    ping_interval=20,
                    ping_timeout=10,
                    max_size=2 * 1024 * 1024,
                )
                asr_parameters = {"sample_rate": 16000, "format": "pcm"}
                if self.language == "en":
                    asr_parameters["language_hints"] = ["en", "zh"]
                else:
                    asr_parameters["language_hints"] = ["zh", "en"]
                await ws.send(json.dumps({
                    "header": {"action": "run-task", "task_id": task_id, "streaming": "duplex"},
                    "payload": {"task_group": "audio", "task": "asr", "function": "recognition",
                                "model": self.asr_model, "parameters": asr_parameters, "input": {}},
                }, separators=(",", ":")))

            async def begin_answer(text: str):
                nonlocal answer_started
                spoken = (text or "").strip()
                if answer_started or not spoken or _is_noise_transcript(spoken, self.language):
                    return
                answer_started = True
                await self.send_json("stt.final", text=spoken)
                self.turn_task = asyncio.create_task(self._answer(spoken))

            async def upload_audio():
                while True:
                    frame = await self.audio_queue.get()
                    if frame is None:
                        await ws.send(json.dumps({
                            "header": {"action": "finish-task", "task_id": task_id, "streaming": "duplex"},
                            "payload": {"input": {}},
                        }, separators=(",", ":")))
                        if self.language != "en":
                            await begin_answer(final_text)
                        return
                    await ws.send(frame)

            sender = asyncio.create_task(upload_audio())
            async for message in ws:
                if not isinstance(message, str):
                    continue
                data = json.loads(message)
                event = data.get("header", {}).get("event", "")
                if event == "task-started":
                    await self.send_json("listen.started")
                text = _last_string(data, "text")
                if text and text != final_text:
                    final_text = text
                    await self.send_json("stt.partial", text=text, sentence_end=_has_true(data, "sentence_end"))
                if event == "task-failed":
                    raise RuntimeError(data.get("header", {}).get("error_message", "ASR task failed"))
                if event == "task-finished":
                    break
            if not answer_started:
                spoken = final_text.strip()
                if spoken and not _is_noise_transcript(spoken, self.language):
                    await self.send_json("stt.final", text=spoken)
                    self.turn_task = asyncio.create_task(self._answer(spoken))
                else:
                    await self.send_json("turn.empty")
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            LOGGER.bind(tag=TAG).error("小云宝 ASR 失败: {}", exc)
            await self.send_json("error", stage="asr", message=str(exc))
        finally:
            if sender and not sender.done():
                sender.cancel()
            if ws:
                await ws.close()
            self.audio_queue = None

    async def _warm_tts(self):
        try:
            await self._ensure_tts()
        except Exception as exc:
            LOGGER.bind(tag=TAG).warning("TTS 预连接失败，回答时再试: {}", exc)

    async def _answer(self, user_text: str):
        asyncio.create_task(self._warm_tts())
        async with self.turn_lock:
            sentence_queue = asyncio.Queue()
            producer = asyncio.create_task(self._stream_llm(user_text, sentence_queue))
            consumer = asyncio.create_task(self._stream_tts(sentence_queue))
            tasks = {producer, consumer}
            try:
                done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_EXCEPTION)
                failure = next(
                    (task.exception() for task in done if not task.cancelled() and task.exception()),
                    None,
                )
                if failure:
                    for task in pending:
                        task.cancel()
                    await asyncio.gather(*pending, return_exceptions=True)
                    raise failure
                await asyncio.gather(*pending)
            except Exception as exc:
                LOGGER.bind(tag=TAG).error("小云宝回答失败: {}", exc)
                await self.send_json("error", stage="reply", message=str(exc))

    async def _stream_llm(self, user_text: str, sentence_queue):
        tag_filter = TagFilter()
        splitter = SentenceSplitter(self.language)
        clean_reply = ""
        emotion_announced = False
        messages = (
            [{"role": "system", "content": ENGLISH_COACH_PROMPT}]
            if self.language == "en"
            else [{"role": "system", "content": self.prompt}]
        )
        messages.extend([*list(self.history), {"role": "user", "content": user_text}])
        vision = self.last_vision if isinstance(self.last_vision, dict) else {}
        vision_age = time.monotonic() - float(vision.get("ts", 0) or 0)
        if vision.get("ok") and vision.get("emotion") and vision.get("emotion") != "neutral" and vision_age <= VISION_CLUE_TTL_S:
            if self.language == "en":
                clue = (
                    f"Visual emotion clue (silent): {vision.get('emotion')} "
                    f"confidence {float(vision.get('confidence', 0)):.2f}. "
                    "Use it only to choose the first-line 【emotion:tag】. "
                    "Do not say the word emotion, the tag, or this clue out loud. "
                    "Stay on the current topic."
                )
            else:
                clue = (
                    f"【视觉情绪线索:{vision.get('emotion')} 置信度{float(vision.get('confidence', 0)):.2f}】"
                    "这是用户此刻的表情，不是诊断。先接住这个情绪，但必须继续刚才的聊天主题，"
                    "不要因为表情就另起一段无关的话。"
                )
            messages.insert(1, {"role": "system", "content": clue})
        body = {
            "model": self.qwen_model,
            "input": {"messages": messages},
            "parameters": {"result_format": "message", "incremental_output": True, "max_tokens": self.max_tokens},
        }
        timeout = aiohttp.ClientTimeout(total=None, sock_connect=10, sock_read=30)
        async with aiohttp.ClientSession(timeout=timeout) as session:
            async with session.post(
                "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation",
                headers={**self.cloud_headers(), "Content-Type": "application/json", "X-DashScope-SSE": "enable"},
                json=body,
            ) as response:
                if response.status != 200:
                    raise RuntimeError(f"千问 HTTP {response.status}: {(await response.text())[:200]}")
                async for raw_line in response.content:
                    line = raw_line.decode("utf-8", errors="ignore").strip()
                    if not line.startswith("data:"):
                        continue
                    payload = line[5:].strip()
                    if not payload or payload == "[DONE]":
                        continue
                    data = json.loads(payload)
                    chunk = _last_string(data, "content")
                    if not chunk:
                        continue
                    clean = tag_filter.feed(chunk)
                    if not emotion_announced and (tag_filter.emotion != "neutral" or "】" in chunk or "]" in chunk):
                        await self.send_json("reply.emotion", emotion=tag_filter.emotion)
                        emotion_announced = True
                    clean_reply += clean
                    for sentence in splitter.feed(clean):
                        spoken = sanitize_spoken_text(sentence, tag_filter)
                        if not spoken:
                            continue
                        await self.send_json("reply.sentence", text=spoken)
                        await sentence_queue.put((spoken, tag_filter.emotion))
        tail = tag_filter.feed("", final=True)
        clean_reply += tail
        if not emotion_announced:
            await self.send_json("reply.emotion", emotion=tag_filter.emotion)
        for sentence in splitter.feed(tail, final=True):
            spoken = sanitize_spoken_text(sentence, tag_filter)
            if not spoken:
                continue
            await self.send_json("reply.sentence", text=spoken)
            await sentence_queue.put((spoken, tag_filter.emotion))
        clean_reply = clean_reply.strip()
        if not clean_reply:
            clean_reply = (
                "Sorry, I didn't understand that. Could you say it again?"
                if self.language == "en"
                else "嗯，我没有听懂，再说一次好吗？"
            )
            await self.send_json("reply.sentence", text=clean_reply)
            await sentence_queue.put((clean_reply, "neutral"))
        self.history.append({"role": "user", "content": user_text})
        self.history.append({"role": "assistant", "content": clean_reply})
        await sentence_queue.put(None)
        await self.send_json("reply.done")

    @staticmethod
    def emotion_params(emotion: str):
        return {
            # Keep the friendly pitch while slowing speech for clear Chinese playback.
            "happy": (0.92, 1.05),
            "sad": (0.82, 0.90),
            "surprise": (0.96, 1.08),
            "shy": (0.92, 0.98),
        }.get(emotion, (0.88, 1.0))

    async def _ensure_tts(self):
        async with self.tts_lock:
            return await self._ensure_tts_locked()

    async def _ensure_tts_locked(self):
        if self.tts_ws is not None:
            try:
                if self.tts_ws.state.name == "OPEN":
                    return self.tts_ws
            except Exception:
                pass
        self.tts_ws = await websockets.connect(
            f"wss://{self.asr_host}/api-ws/v1/inference",
            additional_headers=self.cloud_headers(),
            open_timeout=10,
            close_timeout=3,
            ping_interval=20,
            ping_timeout=10,
            max_size=8 * 1024 * 1024,
        )
        return self.tts_ws

    async def _stream_tts(self, sentence_queue):
        rate, pitch = self.emotion_params("neutral")
        task_id = uuid.uuid4().hex
        ws = await self._ensure_tts()
        await ws.send(json.dumps({
            "header": {"action": "run-task", "task_id": task_id, "streaming": "duplex"},
            "payload": {"task_group": "audio", "task": "tts", "function": "SpeechSynthesizer",
                        "model": self.tts_model, "parameters": {"text_type": "PlainText", "voice": self.tts_voice,
                        "format": "pcm", "sample_rate": 24000, "volume": 50, "rate": rate, "pitch": pitch,
                        "enable_ssml": False}, "input": {}},
        }, ensure_ascii=False, separators=(",", ":")))
        LOGGER.info("小云宝 TTS run-task 已发送，等待 task-started")
        first_wait = asyncio.create_task(sentence_queue.get())
        emotion = "neutral"
        first_text = ""
        while True:
            message = await asyncio.wait_for(ws.recv(), timeout=12)
            if isinstance(message, str):
                data = json.loads(message)
                event = data.get("header", {}).get("event")
                if event == "task-started":
                    LOGGER.info("小云宝 TTS task-started")
                    break
                if event == "task-failed":
                    first_wait.cancel()
                    raise RuntimeError(data.get("header", {}).get("error_message", "TTS start failed"))
        while True:
            first = await first_wait
            if first is None:
                return
            first_text, emotion = first
            first_text = sanitize_spoken_text(first_text)
            if first_text:
                break
            first_wait = asyncio.create_task(sentence_queue.get())
        await self.send_json(
            "audio.start",
            format="opus",
            sample_rate=RELAY_OPUS_SAMPLE_RATE,
            channels=RELAY_OPUS_CHANNELS,
            frame_ms=RELAY_OPUS_FRAME_MS,
            bitrate=RELAY_OPUS_BITRATE,
            emotion=emotion,
        )

        async def send_text(text):
            spoken = sanitize_spoken_text(text)
            if not spoken:
                return
            await ws.send(json.dumps({
                "header": {"action": "continue-task", "task_id": task_id, "streaming": "duplex"},
                "payload": {"input": {"text": spoken}},
            }, ensure_ascii=False, separators=(",", ":")))

        await send_text(first_text)
        relay_started_at = time.monotonic()
        relay_first_pcm_at = None
        relay_pcm_bytes = 0
        relay_opus_bytes = 0
        relay_frame_count = 0
        relay_sequence = 0
        relay_pcm_buffer = bytearray()
        opus_encoder = opuslib_next.Encoder(RELAY_OPUS_SAMPLE_RATE, RELAY_OPUS_CHANNELS, "audio")
        opus_encoder.bitrate = RELAY_OPUS_BITRATE
        opus_encoder.vbr = True

        async def encode_ready_frames(final=False):
            nonlocal relay_sequence, relay_frame_count, relay_opus_bytes
            while len(relay_pcm_buffer) >= RELAY_OPUS_FRAME_BYTES:
                pcm_frame = bytes(relay_pcm_buffer[:RELAY_OPUS_FRAME_BYTES])
                del relay_pcm_buffer[:RELAY_OPUS_FRAME_BYTES]
                encoded = opus_encoder.encode(pcm_frame, RELAY_OPUS_FRAME_SAMPLES)
                relay_opus_bytes += await self.send_opus_packet(
                    encoded, relay_sequence, RELAY_OPUS_FRAME_SAMPLES
                )
                relay_sequence = (relay_sequence + 1) & 0xFFFF
                relay_frame_count += 1
            if final and relay_pcm_buffer:
                if len(relay_pcm_buffer) & 1:
                    raise ValueError("TTS 返回了非偶数字节的 PCM")
                valid_samples = len(relay_pcm_buffer) // 2
                pcm_frame = bytes(relay_pcm_buffer) + bytes(RELAY_OPUS_FRAME_BYTES - len(relay_pcm_buffer))
                relay_pcm_buffer.clear()
                encoded = opus_encoder.encode(pcm_frame, RELAY_OPUS_FRAME_SAMPLES)
                relay_opus_bytes += await self.send_opus_packet(
                    encoded, relay_sequence, valid_samples, flags=1
                )
                relay_sequence = (relay_sequence + 1) & 0xFFFF
                relay_frame_count += 1
        queue_done = False
        finish_sent = False
        pending_get = asyncio.create_task(sentence_queue.get())
        try:
            while True:
                receive = asyncio.create_task(ws.recv())
                wait_set = {receive}
                if pending_get is not None:
                    wait_set.add(pending_get)
                done, _ = await asyncio.wait(
                    wait_set,
                    timeout=20,
                    return_when=asyncio.FIRST_COMPLETED,
                )
                if not done:
                    receive.cancel()
                    await asyncio.gather(receive, return_exceptions=True)
                    raise TimeoutError("TTS 20 秒没有收到音频或状态事件")
                if pending_get is not None and pending_get in done:
                    item = pending_get.result()
                    if item is None:
                        queue_done = True
                        pending_get = None
                    else:
                        await send_text(item[0])
                        pending_get = asyncio.create_task(sentence_queue.get())
                if queue_done and not finish_sent:
                    await ws.send(json.dumps({
                        "header": {"action": "finish-task", "task_id": task_id, "streaming": "duplex"},
                        "payload": {"input": {}},
                    }, separators=(",", ":")))
                    finish_sent = True
                if receive in done:
                    message = receive.result()
                    if isinstance(message, bytes):
                        if relay_first_pcm_at is None:
                            relay_first_pcm_at = time.monotonic()
                        relay_pcm_bytes += len(message)
                        relay_pcm_buffer.extend(message)
                        await encode_ready_frames()
                    else:
                        data = json.loads(message)
                        header = data.get("header", {})
                        event = header.get("event")
                        if event == "task-failed":
                            raise RuntimeError(header.get("error_message", "TTS task failed"))
                        if event == "task-finished":
                            LOGGER.info("小云宝 TTS task-finished")
                            break
                else:
                    # sentence_queue 抢先完成时必须取消并等待 recv；否则下一轮会
                    # 同时存在两个 ws.recv()，websockets 会拒绝并发接收。
                    receive.cancel()
                    try:
                        await receive
                    except asyncio.CancelledError:
                        pass
        except Exception:
            self.tts_ws = None
            try:
                await ws.close()
            except Exception:
                pass
            raise
        finally:
            if pending_get is not None and not pending_get.done():
                pending_get.cancel()
        first_pcm_ms = (
            int((relay_first_pcm_at - relay_started_at) * 1000)
            if relay_first_pcm_at is not None else -1
        )
        await encode_ready_frames(final=True)
        LOGGER.info(
            "小云宝 TTS 下发汇总: PCM={} bytes, Opus={} bytes/{}帧, 首帧={} ms",
            relay_pcm_bytes, relay_opus_bytes, relay_frame_count, first_pcm_ms,
        )
        await self.send_json("audio.end")

    async def speak_text(self, text: str, emotion: str):
        try:
            async with self.turn_lock:
                await self.send_json("reply.emotion", emotion=emotion)
                await self.send_json("reply.sentence", text=text)
                queue = asyncio.Queue()
                await queue.put((text, emotion))
                await queue.put(None)
                await self._stream_tts(queue)
        except Exception as exc:
            LOGGER.error("小云宝直接播报失败: {}", exc)
            await self.send_json("error", stage="tts", message=str(exc))

    async def _accept_vision_jpeg(self, jpeg_bytes: bytes, mode: str):
        del mode
        if not jpeg_bytes:
            return
        remember_preview(jpeg_bytes)
        now = time.monotonic()
        if now - self.last_infer_at < 0.45:
            return
        if self.vision_task is not None and not self.vision_task.done():
            return
        self.last_infer_at = now
        self.vision_task = asyncio.create_task(self._handle_vision_jpeg(jpeg_bytes))

    async def _handle_vision_jpeg(self, jpeg_bytes: bytes):
        if not jpeg_bytes:
            return
        try:
            result = await asyncio.to_thread(self.vision.infer_jpeg, jpeg_bytes)
        except Exception as exc:
            LOGGER.error("视觉推理失败: {}", exc)
            remember_vision(jpeg_bytes, {"ok": False, "message": str(exc), "face": False, "emotion": "neutral"})
            await self.send_json("vision.emotion", ok=False, message=str(exc))
            return
        self.last_vision = {**result, "ts": time.monotonic()}
        remember_vision(jpeg_bytes, result)
        payload = {
            "ok": bool(result.get("ok")),
            "emotion": result.get("emotion", "neutral"),
            "confidence": round(float(result.get("confidence", 0)), 3),
            "face": bool(result.get("face")),
            "orientation": result.get("orientation", ""),
            "message": result.get("message", ""),
        }
        LOGGER.info(
            "视觉识别: ok={} emotion={} conf={} face={} probs={}",
            payload["ok"],
            payload["emotion"],
            payload["confidence"],
            payload["face"],
            result.get("probs") or {},
        )
        await self.send_json("vision.emotion", **payload)
        # 视觉结果只给屏幕文字和下一轮对话线索，不自动开口说话。


async def handle_xiyunbao_connection(websocket, config):
    relay = XiaoyunbaoRelay(websocket, config)
    await relay.run()
