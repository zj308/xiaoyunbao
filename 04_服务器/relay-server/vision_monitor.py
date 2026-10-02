"""Desktop live view of Xiaoyunbao cloud vision frames.

Double-click 桌面「小云宝视觉调试.bat」. Uses SSH, no public webpage.
Photos stay in server RAM; this window only displays them.
"""

from __future__ import annotations

import io
import json
import threading
import time
import traceback
import tkinter as tk
from pathlib import Path
from tkinter import messagebox

import paramiko
from PIL import Image, ImageTk

ROOT = Path(__file__).resolve().parent
CONFIG_PATH = ROOT / "vision_monitor.local.json"
LABELS = ("angry", "fear", "happy", "neutral", "surprise")
LABEL_CN = {
    "angry": "生气",
    "fear": "紧张",
    "happy": "开心",
    "neutral": "平静",
    "surprise": "惊讶",
}


def load_config():
    return json.loads(CONFIG_PATH.read_text(encoding="utf-8"))


def ssh_http_get(transport, path: str, debug_port: int, secret: str) -> tuple[int, bytes]:
    joiner = "&" if "?" in path else "?"
    channel = transport.open_channel(
        "direct-tcpip",
        dest_addr=("127.0.0.1", debug_port),
        src_addr=("127.0.0.1", 0),
    )
    try:
        channel.settimeout(6)
        request = (
            f"GET {path}{joiner}secret={secret} HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Connection: close\r\n"
            "\r\n"
        )
        channel.sendall(request.encode("ascii"))
        data = b""
        while True:
            try:
                chunk = channel.recv(65536)
            except Exception:
                break
            if not chunk:
                break
            data += chunk
            if len(data) > 400000:
                break
        header, sep, body = data.partition(b"\r\n\r\n")
        if not sep:
            return 0, b""
        status = 0
        parts = header.split(b"\r\n", 1)[0].split()
        if len(parts) >= 2:
            try:
                status = int(parts[1])
            except ValueError:
                status = 0
        length = None
        for line in header.split(b"\r\n"):
            if line.lower().startswith(b"content-length:"):
                length = int(line.split(b":", 1)[1].strip() or 0)
                break
        if length is not None:
            body = body[:length]
        return status, body
    finally:
        try:
            channel.close()
        except Exception:
            pass


class MjpegStream:
    def __init__(self, transport, debug_port: int, secret: str):
        self.channel = transport.open_channel(
            "direct-tcpip",
            dest_addr=("127.0.0.1", debug_port),
            src_addr=("127.0.0.1", 0),
        )
        self.channel.settimeout(3)
        request = (
            f"GET /vision/stream?secret={secret} HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Connection: keep-alive\r\n"
            "\r\n"
        )
        self.channel.sendall(request.encode("ascii"))
        self.buf = b""
        self._read_headers()

    def _read_headers(self):
        while b"\r\n\r\n" not in self.buf:
            chunk = self.channel.recv(65536)
            if not chunk:
                raise RuntimeError("视频流没有响应")
            self.buf += chunk
        header, _, rest = self.buf.partition(b"\r\n\r\n")
        first = header.split(b"\r\n", 1)[0]
        if b" 200 " not in first and not first.endswith(b" 200"):
            raise RuntimeError(f"视频流失败: {first.decode('ascii', 'ignore')}")
        self.buf = rest

    def _recv_more(self) -> bool:
        chunk = self.channel.recv(65536)
        if not chunk:
            return False
        self.buf += chunk
        if len(self.buf) > 2 * 1024 * 1024:
            self.buf = self.buf[-512 * 1024:]
        return True

    def read_frame(self) -> bytes:
        while True:
            start = self.buf.find(b"\xff\xd8")
            if start < 0:
                self.buf = b""
                if not self._recv_more():
                    raise RuntimeError("视频流断开")
                continue
            if start > 0:
                self.buf = self.buf[start:]
                start = 0
            end = self.buf.find(b"\xff\xd9", 2)
            if end > start:
                jpeg = self.buf[start:end + 2]
                self.buf = self.buf[end + 2:]
                if len(jpeg) > 32:
                    return jpeg
                continue
            if not self._recv_more():
                raise RuntimeError("视频流断开")

    def close(self):
        try:
            self.channel.close()
        except Exception:
            pass


class VisionMonitor:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.cfg = load_config()
        self.transport = None
        self.client = None
        self.stream = None
        self.stop = False
        self.photo_refs = {}
        self.lock = threading.Lock()
        self.latest_raw = b""
        self.latest_used = b""
        self.latest_face = b""
        self.latest_info = "等待识别信息..."
        self.latest_color = "#7dffa3"
        self.latest_probs = {}
        self.frame_count = 0
        self.raw_changed_at = 0.0
        self.status_text = "正在连接云端..."

        root.title("小云宝视觉调试")
        root.geometry("1180x760")
        root.configure(bg="#111111")

        self.status = tk.Label(
            root, text=self.status_text, fg="#eeeeee", bg="#111111",
            font=("Microsoft YaHei", 12),
        )
        self.status.pack(anchor="w", padx=16, pady=(12, 4))
        self.info = tk.Label(
            root, text="", fg="#7dffa3", bg="#111111",
            font=("Microsoft YaHei", 14), justify="left",
        )
        self.info.pack(anchor="w", padx=16, pady=(0, 8))

        pics = tk.Frame(root, bg="#111111")
        pics.pack(fill="both", expand=True, padx=12)
        self.img_labels = {}
        for key, title, expand in (
            ("raw", "实时画面", True),
            ("used", "识别用图", False),
            ("face", "裁出的整张脸", False),
        ):
            card = tk.Frame(pics, bg="#1c1c1c")
            card.pack(side="left", fill="both", expand=expand, padx=6)
            tk.Label(
                card, text=title, fg="#dddddd", bg="#1c1c1c",
                font=("Microsoft YaHei", 11),
            ).pack()
            label = tk.Label(card, bg="#000000")
            label.pack(fill="both", expand=True, padx=6, pady=6)
            self.img_labels[key] = label

        self.bars = tk.Frame(root, bg="#111111")
        self.bars.pack(fill="x", padx=16, pady=10)
        self.bar_vars = {}
        for name in LABELS:
            row = tk.Frame(self.bars, bg="#111111")
            row.pack(fill="x", pady=2)
            tk.Label(
                row, text=f"{LABEL_CN[name]:<4} {name}", width=16, anchor="w",
                fg="#cccccc", bg="#111111", font=("Consolas", 11),
            ).pack(side="left")
            canvas = tk.Canvas(row, height=12, bg="#333333", highlightthickness=0)
            canvas.pack(side="left", fill="x", expand=True, padx=8)
            self.bar_vars[name] = canvas

        threading.Thread(target=self.stream_loop, daemon=True).start()
        threading.Thread(target=self.meta_loop, daemon=True).start()
        root.protocol("WM_DELETE_WINDOW", self.close)
        self.tick()

    def close(self):
        self.stop = True
        self.disconnect()
        self.root.destroy()

    def connect(self):
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        client.connect(
            self.cfg["host"],
            port=int(self.cfg.get("port", 22)),
            username=self.cfg.get("user", "root"),
            password=self.cfg["password"],
            timeout=20,
            allow_agent=False,
            look_for_keys=False,
            banner_timeout=20,
        )
        transport = client.get_transport()
        transport.set_keepalive(10)
        self.client = client
        self.transport = transport
        self.stream = MjpegStream(
            transport,
            int(self.cfg.get("debug_port", 8001)),
            str(self.cfg.get("secret", "")),
        )

    def disconnect(self):
        try:
            if self.stream:
                self.stream.close()
        except Exception:
            pass
        self.stream = None
        try:
            if self.client:
                self.client.close()
        except Exception:
            pass
        self.client = None
        self.transport = None

    def set_status(self, text: str):
        self.status_text = text

    def stream_loop(self):
        last = b""
        while not self.stop:
            try:
                if self.transport is None or not self.transport.is_active() or self.stream is None:
                    self.set_status("正在连接云端...")
                    self.disconnect()
                    self.connect()
                    self.set_status("已连接，等待摄像头画面...")
                jpeg = self.stream.read_frame()
                if jpeg and jpeg != last:
                    last = jpeg
                    with self.lock:
                        self.latest_raw = jpeg
                        self.frame_count += 1
                        self.raw_changed_at = time.monotonic()
            except Exception as exc:
                self.set_status(f"画面中断，重连: {exc}")
                self.disconnect()
                time.sleep(0.8)

    def meta_loop(self):
        while not self.stop:
            try:
                if self.transport is None or not self.transport.is_active():
                    time.sleep(0.3)
                    continue
                status, body = ssh_http_get(
                    self.transport,
                    "/vision/latest.json",
                    int(self.cfg.get("debug_port", 8001)),
                    str(self.cfg.get("secret", "")),
                )
                if status != 200:
                    time.sleep(0.4)
                    continue
                data = json.loads(body.decode("utf-8"))
                meta = data.get("meta") or {}
                age = data.get("age_s")
                age_text = f"{age:.1f} 秒前" if isinstance(age, (int, float)) else "无"
                emotion = meta.get("emotion") or "-"
                conf = meta.get("confidence")
                conf_text = f"{float(conf):.3f}" if conf is not None else "-"
                face = "有人脸" if meta.get("face") else "没有人脸"
                orientation = meta.get("orientation") or "-"
                message = meta.get("message") or ""
                color = "#7dffa3" if meta.get("ok") else "#ff8a8a"
                info = (
                    f"更新 {age_text}    情绪 {LABEL_CN.get(emotion, emotion)} ({emotion})    "
                    f"置信度 {conf_text}    {face}    方向 {orientation}\n{message}"
                )
                used_status, used = ssh_http_get(
                    self.transport, "/vision/used.jpg",
                    int(self.cfg.get("debug_port", 8001)), str(self.cfg.get("secret", "")),
                )
                face_status, face_jpg = ssh_http_get(
                    self.transport, "/vision/face.jpg",
                    int(self.cfg.get("debug_port", 8001)), str(self.cfg.get("secret", "")),
                )
                with self.lock:
                    self.latest_info = info
                    self.latest_color = color
                    self.latest_probs = dict(meta.get("probs") or {})
                    if used_status == 200 and used:
                        self.latest_used = used
                    if face_status == 200 and face_jpg:
                        self.latest_face = face_jpg
            except Exception:
                time.sleep(0.8)
                continue
            time.sleep(0.35)

    def show_image(self, key: str, jpeg: bytes, max_size: tuple[int, int]):
        if not jpeg:
            return
        image = Image.open(io.BytesIO(jpeg)).convert("RGB")
        image.thumbnail(max_size)
        photo = ImageTk.PhotoImage(image)
        self.photo_refs[key] = photo
        self.img_labels[key].configure(image=photo)

    def show_bars(self, probs: dict):
        for name in LABELS:
            canvas = self.bar_vars[name]
            canvas.update_idletasks()
            width = max(canvas.winfo_width(), 100)
            value = float(probs.get(name, 0) or 0)
            canvas.delete("all")
            canvas.create_rectangle(0, 0, int(width * value), 12, fill="#4ea1ff", outline="")

    def tick(self):
        if self.stop:
            return
        with self.lock:
            raw = self.latest_raw
            used = self.latest_used
            face = self.latest_face
            info = self.latest_info
            color = self.latest_color
            probs = dict(self.latest_probs)
            frames = self.frame_count
            changed = self.raw_changed_at
        age_ms = (time.monotonic() - changed) * 1000 if changed else None
        if raw and age_ms is not None and age_ms < 2500:
            self.status.configure(text=f"实时画面已更新  第 {frames} 帧  {age_ms:.0f} ms 前")
        elif raw:
            self.status.configure(text=f"画面停住了  第 {frames} 帧  {age_ms:.0f} ms 前。设备可能没在上传。")
        else:
            self.status.configure(text=self.status_text or "还没有摄像头画面。设备要联网，摄像头要插上。")
        self.info.configure(text=info, fg=color)
        self.show_bars(probs)
        try:
            if raw:
                self.show_image("raw", raw, (640, 480))
            if used:
                self.show_image("used", used, (280, 220))
            if face:
                self.show_image("face", face, (280, 220))
        except Exception:
            pass
        self.root.after(50, self.tick)


def main():
    log = ROOT / "vision_monitor.log"
    try:
        if not CONFIG_PATH.exists():
            raise FileNotFoundError(f"找不到 {CONFIG_PATH}")
        root = tk.Tk()
        root.attributes("-topmost", True)
        root.after(2500, lambda: root.attributes("-topmost", False))
        root.lift()
        root.focus_force()
        VisionMonitor(root)
        root.mainloop()
    except Exception as exc:
        text = traceback.format_exc()
        try:
            log.write_text(text, encoding="utf-8")
        except Exception:
            pass
        try:
            messagebox.showerror("小云宝视觉调试", f"启动失败: {exc}\n\n日志: {log}")
        except Exception:
            print(text)
            input("启动失败，按回车关闭")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
