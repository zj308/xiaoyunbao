"""iCAN Fusion/Fear/Local emotion inference for Xiaoyunbao relay.

ONNX files stay on the PC/cloud. ESP32 only captures JPEG QVGA frames and
uploads them. This module never writes images to disk.
"""

from __future__ import annotations

import io
import time
from pathlib import Path

try:
    import numpy as np
    import onnxruntime as ort
    from PIL import Image
    _VISION_DEPS_ERROR = ""
except Exception as exc:
    np = None
    ort = None
    Image = None
    _VISION_DEPS_ERROR = str(exc)


EMOTION_LABELS = ("angry", "fear", "happy", "neutral", "surprise")
CLASSWISE_WEIGHTS = None
if np is not None:
    CLASSWISE_WEIGHTS = np.array(
        [
            [0.685, 0.115, 0.200],
            [0.625, 0.200, 0.175],
            [0.750, 0.055, 0.195],
            [0.760, 0.095, 0.145],
            [0.875, 0.040, 0.085],
        ],
        dtype=np.float32,
    )

CARE_SCRIPTS = {
    "angry": (
        "sad",
        "我看到你好像有点生气。我们先停三秒，把这件事拆成眼前能做的一步。"
        "要不要告诉我最卡住的那一点？我陪你想一个马上能做的小办法。",
    ),
    "fear": (
        "sad",
        "你看起来有点紧张。我在这儿，先不用一次想完全部。"
        "把最担心的那一件事说出来，我们一起做一个六十秒就能开始的小步骤。",
    ),
    "surprise": (
        "surprise",
        "咦，你好像吃了一惊。发生什么了吗？我听着，你想说多少就说多少。",
    ),
}

LEFT_EYE = (33, 160, 158, 133, 153, 144)
RIGHT_EYE = (362, 385, 387, 263, 373, 380)
LEFT_BROW = (70, 63, 105, 66, 107)
RIGHT_BROW = (336, 296, 334, 293, 300)
MOUTH = (61, 291, 0, 17, 13, 14, 78, 308)
FACE = (10, 152, 234, 454)
# MediaPipe face oval. mesh[:17] is nose/mouth, which cropped only half a face.
FACE_OVAL = (
    10, 338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288, 397, 365,
    379, 378, 400, 377, 152, 148, 176, 149, 150, 136, 172, 58, 132, 93,
    234, 127, 162, 21, 54, 103, 67, 109,
)


def _softmax(logits: np.ndarray) -> np.ndarray:
    x = logits.astype(np.float32).reshape(-1)
    x = x - np.max(x)
    e = np.exp(x)
    return e / np.clip(e.sum(), 1e-8, None)


def _nchw(image: Image.Image, mean, std) -> np.ndarray:
    arr = np.asarray(image.resize((224, 224), Image.Resampling.BILINEAR), dtype=np.float32) / 255.0
    if arr.ndim == 2:
        arr = np.stack([arr, arr, arr], axis=-1)
    arr = (arr - np.array(mean, dtype=np.float32)) / np.array(std, dtype=np.float32)
    return np.transpose(arr, (2, 0, 1))[None, ...]


def _crop_points(image: Image.Image, points, pad_x, pad_y) -> Image.Image:
    w, h = image.size
    xs = [p[0] * w for p in points]
    ys = [p[1] * h for p in points]
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    bw = max(max_x - min_x, 8)
    bh = max(max_y - min_y, 8)
    left = int(max(0, min_x - bw * pad_x))
    top = int(max(0, min_y - bh * pad_y))
    right = int(min(w, max_x + bw * pad_x))
    bottom = int(min(h, max_y + bh * pad_y))
    if right - left < 8 or bottom - top < 8:
        return image
    return image.crop((left, top, right, bottom))


def _dist(a, b) -> float:
    return float(np.hypot(a[0] - b[0], a[1] - b[1]))


def _ear(pts) -> float:
    return (_dist(pts[1], pts[5]) + _dist(pts[2], pts[4])) / (2.0 * max(_dist(pts[0], pts[3]), 1e-6))


def landmark16(mesh) -> np.ndarray:
    def p(i):
        return float(mesh[i][0]), float(mesh[i][1])

    l_eye = [p(i) for i in LEFT_EYE]
    r_eye = [p(i) for i in RIGHT_EYE]
    l_brow = [p(i) for i in LEFT_BROW]
    r_brow = [p(i) for i in RIGHT_BROW]
    mouth = [p(i) for i in MOUTH]
    face_h = max(_dist(p(10), p(152)), 1e-6)
    face_w = max(_dist(p(234), p(454)), 1e-6)
    l_eye_c = np.mean(l_eye, axis=0)
    r_eye_c = np.mean(r_eye, axis=0)
    l_brow_c = np.mean(l_brow, axis=0)
    r_brow_c = np.mean(r_brow, axis=0)
    mouth_c = np.mean(mouth, axis=0)
    feat = np.array(
        [
            _ear(l_eye),
            _ear(r_eye),
            _dist(p(13), p(14)) / face_h,
            _dist(p(61), p(291)) / face_w,
            abs(l_brow_c[1] - l_eye_c[1]) / face_h,
            abs(r_brow_c[1] - r_eye_c[1]) / face_h,
            _dist(l_brow[-1], r_brow[0]) / face_w,
            ((p(61)[1] + p(291)[1]) / 2 - p(13)[1]) / face_h,
            _dist(p(1), mouth_c) / face_h if 1 < len(mesh) else 0.2,
            _ear(l_eye) - _ear(r_eye),
            (l_brow_c[1] + r_brow_c[1]) / 2 / face_h,
            _dist(p(152), mouth_c) / face_h,
            (l_eye_c[1] - r_eye_c[1]) / face_h,
            (p(1)[0] - 0.5) if 1 < len(mesh) else 0.0,
            abs(p(0)[1] - p(1)[1]) / face_h if 1 < len(mesh) else 0.1,
            _dist((l_eye_c + r_eye_c) / 2, mouth_c) / face_h,
        ],
        dtype=np.float32,
    )
    return (feat - 0.35) / 0.25


def fallback_mesh(width_ratio=1.0) -> list:
    # Handheld companion camera: assume a near-frontal face fills the frame.
    return [(x, y) for y in np.linspace(0.18, 0.86, 22) for x in np.linspace(0.18, 0.82, 22)][:478]


def _safe_point(mesh, index, default):
    if mesh and index < len(mesh):
        return mesh[index]
    return default


class EmotionVision:
    def __init__(self, model_dir: Path):
        self.model_dir = Path(model_dir)
        self.ready = False
        self.reason = "not loaded"
        self._fusion = None
        self._fear = None
        self._local = None
        self._mesh = None
        self._mesh_kind = ""
        self._last_orientation = "original"
        self._load()

    def _load(self):
        if _VISION_DEPS_ERROR:
            self.reason = f"missing python deps: {_VISION_DEPS_ERROR}"
            print(f"视觉情绪模型未就绪: {self.reason}")
            return
        fusion = self.model_dir / "fusion_v1_int8_realdata_qdq.onnx"
        fear = self.model_dir / "fear_aux_v2_int8_realdata_qdq.onnx"
        local = self.model_dir / "local_visual_v1_fp32_realdata.onnx"
        if not fusion.exists() or not local.exists():
            self.reason = f"missing models in {self.model_dir}"
            print(f"视觉情绪模型未就绪: {self.reason}")
            return
        so = ort.SessionOptions()
        so.log_severity_level = 3
        self._fusion = ort.InferenceSession(str(fusion), so, providers=["CPUExecutionProvider"])
        self._local = ort.InferenceSession(str(local), so, providers=["CPUExecutionProvider"])
        # Delivered Fusion ONNX has no 1280-d image_feature output, so Fear Aux
        # cannot be fed as specified. Classwise fusion uses Fusion + Local.
        self._fear = None
        self._load_face_mesh()
        self.ready = True
        self.reason = "ready" + ("" if self._mesh is not None else ", mediapipe missing")
        print(f"视觉情绪模型已加载: {self.model_dir} ({self.reason})")

    def _load_face_mesh(self):
        self._mesh = None
        self._mesh_kind = ""
        try:
            from mediapipe.tasks.python.core.base_options import BaseOptions
            from mediapipe.tasks.python.vision import FaceLandmarker, FaceLandmarkerOptions, RunningMode

            task = self.model_dir / "face_landmarker.task"
            if not task.exists():
                import urllib.request

                url = (
                    "https://storage.googleapis.com/mediapipe-models/"
                    "face_landmarker/face_landmarker/float16/1/face_landmarker.task"
                )
                print(f"下载人脸关键点模型: {url}")
                urllib.request.urlretrieve(url, task)
            options = FaceLandmarkerOptions(
                base_options=BaseOptions(model_asset_path=str(task)),
                running_mode=RunningMode.IMAGE,
                num_faces=1,
                min_face_detection_confidence=0.3,
                min_face_presence_confidence=0.3,
            )
            self._mesh = FaceLandmarker.create_from_options(options)
            self._mesh_kind = "tasks"
            return
        except Exception as exc:
            print(f"MediaPipe Tasks 人脸检测加载失败: {exc}")
            self._mesh = None
            self._mesh_kind = ""

    def _detect(self, image: Image.Image):
        if self._mesh is None:
            return None, False
        rgb = np.ascontiguousarray(np.asarray(image.convert("RGB")))
        if self._mesh_kind == "tasks":
            from mediapipe import Image as MpImage
            from mediapipe import ImageFormat

            result = self._mesh.detect(MpImage(image_format=ImageFormat.SRGB, data=rgb))
            if not result.face_landmarks:
                return None, False
            mesh = [(float(pt.x), float(pt.y)) for pt in result.face_landmarks[0]]
            return mesh, True
        result = self._mesh.process(rgb)
        if not result.multi_face_landmarks:
            return None, False
        lm = result.multi_face_landmarks[0].landmark
        mesh = [(pt.x, pt.y) for pt in lm]
        return mesh, True

    def _crop_face(self, image: Image.Image, mesh) -> Image.Image:
        points = [mesh[i] for i in FACE_OVAL if i < len(mesh)]
        if len(points) < 8:
            points = [mesh[i] for i in FACE if i < len(mesh)]
        if 10 < len(mesh) and 152 < len(mesh):
            top_x, top_y = mesh[10]
            chin_x, chin_y = mesh[152]
            face_h = abs(chin_y - top_y) or 0.4
            points.append((top_x, top_y - 0.22 * face_h))
        return _crop_points(image, points, 0.32, 0.22)

    def _oriented_images(self, image: Image.Image):
        wide = image.resize((640, 480), Image.Resampling.BILINEAR)
        # Firmware already vflips for a natural preview. Detect on the original
        # first so the debug "used" image is not mirrored.
        return (
            ("original", wide),
            ("vflip", wide.transpose(Image.Transpose.FLIP_TOP_BOTTOM)),
            ("rot180", wide.transpose(Image.Transpose.ROTATE_180)),
        )

    def _mesh_to_original(self, mesh, orientation):
        if orientation == "vflip":
            return [(x, 1.0 - y) for x, y in mesh]
        if orientation == "rot180":
            return [(1.0 - x, 1.0 - y) for x, y in mesh]
        return mesh

    def infer_jpeg(self, jpeg_bytes: bytes) -> dict:
        if not self.ready:
            return {"ok": False, "message": self.reason}
        raw = Image.open(io.BytesIO(jpeg_bytes)).convert("RGB")
        image = None
        mesh = None
        detected = False
        orientation = "none"
        original = None
        for name, variant in self._oriented_images(raw):
            if original is None:
                original = variant
            mesh, detected = self._detect(variant)
            if detected:
                image = variant
                orientation = name
                break
        if original is None:
            original = raw.resize((640, 480), Image.Resampling.BILINEAR)
        if mesh is None or image is None:
            return {
                "ok": False,
                "emotion": "neutral",
                "confidence": 0.0,
                "face": False,
                "message": "no face",
                "orientation": orientation,
                "_debug_used": original,
                "_debug_face": None,
            }
        self._last_orientation = orientation
        feat = landmark16(mesh)
        display_mesh = self._mesh_to_original(mesh, orientation)
        face = self._crop_face(original, display_mesh)
        gray_face = Image.merge("RGB", [face.convert("L")] * 3)
        left_eye = _crop_points(image, [mesh[i] if i < len(mesh) else mesh[0] for i in LEFT_BROW + LEFT_EYE], 0.55, 1.00)
        right_eye = _crop_points(image, [mesh[i] if i < len(mesh) else mesh[0] for i in RIGHT_BROW + RIGHT_EYE], 0.55, 1.00)
        mouth = _crop_points(image, [mesh[i] if i < len(mesh) else mesh[0] for i in MOUTH], 0.80, 1.20)

        fusion_in = {
            "image": _nchw(gray_face, (0.5, 0.5, 0.5), (0.5, 0.5, 0.5)),
            "landmark": feat[None, :],
        }
        p_fusion = _softmax(self._fusion.run(None, fusion_in)[0])
        p_local = _softmax(
            self._local.run(
                None,
                {
                    "eye_left": _nchw(left_eye, (0.485, 0.456, 0.406), (0.229, 0.224, 0.225)),
                    "eye_right": _nchw(right_eye, (0.485, 0.456, 0.406), (0.229, 0.224, 0.225)),
                    "mouth": _nchw(mouth, (0.485, 0.456, 0.406), (0.229, 0.224, 0.225)),
                },
            )[0]
        )
        if self._fear is not None:
            # Delivered Fusion ONNX only exposes 5-class logits, not the 1280-d
            # image feature. Keep Fear Aux in the graph with a safe stand-in so
            # classwise weights stay in spec; its column then follows Fusion.
            p_fear = _softmax(
                self._fear.run(
                    None,
                    {
                        "image_feature": np.zeros((1, 1280), dtype=np.float32),
                        "landmark": feat[None, :],
                    },
                )[0]
            )
            fused = CLASSWISE_WEIGHTS[:, 0] * p_fusion + CLASSWISE_WEIGHTS[:, 1] * p_fear + CLASSWISE_WEIGHTS[:, 2] * p_local
        else:
            fused = CLASSWISE_WEIGHTS[:, 0] * p_fusion + CLASSWISE_WEIGHTS[:, 2] * p_local
        fused = fused / np.clip(fused.sum(), 1e-8, None)
        fused, index = _calibrate_emotion(fused, mesh)
        label = EMOTION_LABELS[index]
        return {
            "ok": True,
            "emotion": label,
            "confidence": float(fused[index]),
            "probs": {name: float(fused[i]) for i, name in enumerate(EMOTION_LABELS)},
            "face": True,
            "orientation": orientation,
            "source": "fusion+local" + ("+fear" if self._fear is not None else ""),
            "_debug_used": original,
            "_debug_face": face,
        }


def _calibrate_emotion(fused: np.ndarray, mesh) -> tuple:
    """QVGA companion shots make Fusion dump mass onto happy. Rebalance."""
    happy, fear, angry, neutral, surprise = 2, 1, 0, 3, 4
    out = fused.astype(np.float32).copy()
    out[happy] *= 0.38
    out[neutral] *= 1.28
    out[angry] *= 1.22
    out[fear] *= 1.22
    out[surprise] *= 1.08
    if mesh is not None and len(mesh) > 291:
        mouth_h = _dist(mesh[13], mesh[14]) / max(_dist(mesh[10], mesh[152]), 1e-6)
        mouth_w = _dist(mesh[61], mesh[291]) / max(_dist(mesh[234], mesh[454]), 1e-6)
        if mouth_h < 0.04:
            out[happy] *= 0.40
            out[surprise] *= 0.50
            out[neutral] *= 1.20
        if mouth_w < 0.30:
            out[happy] *= 0.65
    out = out / np.clip(out.sum(), 1e-8, None)
    index = int(np.argmax(out))
    ranked = np.sort(out)[::-1]
    margin = float(ranked[0] - ranked[1]) if len(ranked) > 1 else float(out[index])
    if index == happy and margin < 0.18:
        out[happy] *= 0.45
        out = out / np.clip(out.sum(), 1e-8, None)
        index = int(np.argmax(out))
    return out, index


_VISION = None
_LAST_CARE = 0.0


def get_vision(config: dict) -> EmotionVision:
    global _VISION
    if _VISION is None:
        relay = config.get("xiyunbao", {})
        model_dir = Path(relay.get("emotion_model_dir", Path(__file__).resolve().parents[1] / "models" / "emotion"))
        _VISION = EmotionVision(model_dir)
    return _VISION


def should_speak_care(result: dict, busy: bool) -> tuple[bool, str, str]:
    global _LAST_CARE
    if busy or not result.get("ok") or not result.get("face"):
        return False, "", ""
    emotion = result.get("emotion", "neutral")
    confidence = float(result.get("confidence", 0))
    probs = result.get("probs") or {}
    ranked = sorted((float(v) for v in probs.values()), reverse=True)
    margin = ranked[0] - ranked[1] if len(ranked) > 1 else confidence
    if emotion not in CARE_SCRIPTS or confidence < 0.46 or margin < 0.08:
        return False, "", ""
    now = time.monotonic()
    if now - _LAST_CARE < 90:
        return False, "", ""
    _LAST_CARE = now
    ui_emotion, text = CARE_SCRIPTS[emotion]
    return True, ui_emotion, text
