from collections import deque
import argparse
from pathlib import Path
import struct
from PIL import Image

UI_PATH = Path(r"E:\ui\ui1.1.bmp")
FRAME_DIR = Path(r"E:\ui\打招呼")
OUT_DIR = Path(r"E:\ui\anim")


def remove_connected_black(image: Image.Image) -> Image.Image:
    image = image.convert("RGBA")
    width, height = image.size
    pixels = image.load()
    seen = bytearray(width * height)
    queue = deque()
    for x in range(width):
        queue.extend(((x, 0), (x, height - 1)))
    for y in range(height):
        queue.extend(((0, y), (width - 1, y)))
    while queue:
        x, y = queue.popleft()
        index = y * width + x
        if seen[index]:
            continue
        seen[index] = 1
        r, g, b, _ = pixels[x, y]
        if max(r, g, b) > 42:
            continue
        pixels[x, y] = (r, g, b, 0)
        if x > 0: queue.append((x - 1, y))
        if x + 1 < width: queue.append((x + 1, y))
        if y > 0: queue.append((x, y - 1))
        if y + 1 < height: queue.append((x, y + 1))
    return image


def write_rgb565(image: Image.Image, path: Path) -> None:
    rgb = image.convert("RGB")
    values = bytearray()
    for r, g, b in rgb.getdata():
        value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        values.extend(struct.pack("<H", value))
    path.write_bytes(values)


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert seven greeting PNG frames to ESP32 RGB565 files")
    parser.add_argument("--ui", type=Path, default=UI_PATH, help="240x320 UI image, e.g. F:\\ui\\ui1.1.bmp")
    parser.add_argument("--frames", type=Path, default=FRAME_DIR, help="directory containing exactly seven PNG frames")
    parser.add_argument("--out", type=Path, default=OUT_DIR, help="output directory on the TF card")
    args = parser.parse_args()
    ui_path = args.ui
    frame_dir = args.frames
    out_dir = args.out
    if not ui_path.exists():
        raise SystemExit(f"missing UI image: {ui_path}")
    source_ui = Image.open(ui_path).convert("RGB").resize((240, 320), Image.Resampling.LANCZOS)
    frames = sorted(frame_dir.glob("*.png*"))
    if len(frames) != 7:
        raise SystemExit(f"expected 7 PNG frames in {frame_dir}, found {len(frames)}")
    out_dir.mkdir(parents=True, exist_ok=True)
    # 先求七帧人物的共同可见范围，再统一缩放；逐帧按自身边界缩放会造成大小忽大忽小。
    processed_frames = [remove_connected_black(Image.open(frame_path)) for frame_path in frames]
    visible_boxes = [frame.getbbox() for frame in processed_frames]
    visible_boxes = [box for box in visible_boxes if box is not None]
    if not visible_boxes:
        raise SystemExit("all greeting frames are fully transparent/black")
    union_box = (
        min(box[0] for box in visible_boxes),
        min(box[1] for box in visible_boxes),
        max(box[2] for box in visible_boxes),
        max(box[3] for box in visible_boxes),
    )
    union_width = union_box[2] - union_box[0]
    union_height = union_box[3] - union_box[1]
    target_width, target_height = 150, 160
    scale = min(target_width / union_width, target_height / union_height)
    scaled_width = max(1, round(union_width * scale))
    scaled_height = max(1, round(union_height * scale))

    previews = []
    for number, frame in enumerate(processed_frames, 1):
        frame = frame.crop(union_box).resize((scaled_width, scaled_height), Image.Resampling.LANCZOS)
        normalized = Image.new("RGBA", (target_width, target_height), (0, 0, 0, 0))
        normalized.alpha_composite(
            frame,
            ((target_width - scaled_width) // 2, (target_height - scaled_height) // 2),
        )
        composed = source_ui.convert("RGBA")
        # ui1.1.bmp 已经包含静态人物；先清除人物区域，再放入本帧，避免两个表情重叠。
        # 该区域位于标题下方、蓝色气泡上方，背景为黑色。
        composed.paste((0, 0, 0, 255), (0, 38, 240, 199))
        composed.alpha_composite(normalized, (0, 42))
        write_rgb565(composed, out_dir / f"greeting{number:02d}.rgb565")
        previews.append(composed.convert("RGB").resize((120, 160)))
    montage = Image.new("RGB", (120 * 7, 160), "black")
    for number, preview in enumerate(previews):
        montage.paste(preview, (number * 120, 0))
    montage.save(Path(__file__).parent / "greeting_montage.png")
    print(f"wrote 7 frames to {out_dir}")


if __name__ == "__main__":
    main()
