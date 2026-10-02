from collections import deque
from pathlib import Path
import struct
from PIL import Image

UI_PATH = Path(r"E:\ui\ui1.1.bmp")
FRAME_DIR = Path(r"C:\Users\xijie\AppData\Local\Temp\xyb_listen_anim\src")
OUT_DIR = Path(r"E:\ui\anim")
PREVIEW_DIR = Path(__file__).parent


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
        if max(r, g, b) > 48:
            continue
        pixels[x, y] = (r, g, b, 0)
        if x > 0:
            queue.append((x - 1, y))
        if x + 1 < width:
            queue.append((x + 1, y))
        if y > 0:
            queue.append((x, y - 1))
        if y + 1 < height:
            queue.append((x, y + 1))
    return image


def write_rgb565(image: Image.Image, path: Path) -> None:
    rgb = image.convert("RGB")
    values = bytearray()
    for r, g, b in rgb.getdata():
        value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        values.extend(struct.pack("<H", value))
    path.write_bytes(values)


def main() -> None:
    source_ui = Image.open(UI_PATH).convert("RGB").resize((240, 320), Image.Resampling.LANCZOS)
    frames = [FRAME_DIR / f"{number}.jpg" for number in range(1, 8)]
    missing = [str(path) for path in frames if not path.exists()]
    if missing:
        raise SystemExit("missing listen frames: " + ", ".join(missing))
    processed_frames = [remove_connected_black(Image.open(path)) for path in frames]
    visible_boxes = [frame.getbbox() for frame in processed_frames]
    visible_boxes = [box for box in visible_boxes if box is not None]
    if not visible_boxes:
        raise SystemExit("all listen frames are fully transparent/black")
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
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    previews = []
    for number, frame in enumerate(processed_frames, 1):
        frame = frame.crop(union_box).resize((scaled_width, scaled_height), Image.Resampling.NEAREST)
        normalized = Image.new("RGBA", (target_width, target_height), (0, 0, 0, 0))
        normalized.alpha_composite(
            frame,
            ((target_width - scaled_width) // 2, (target_height - scaled_height) // 2),
        )
        composed = source_ui.convert("RGBA")
        composed.paste((0, 0, 0, 255), (0, 38, 240, 199))
        composed.alpha_composite(normalized, (0, 42))
        out_path = OUT_DIR / f"listen{number:02d}.rgb565"
        write_rgb565(composed, out_path)
        previews.append(composed.convert("RGB").resize((120, 160), Image.Resampling.NEAREST))
        print(f"wrote {out_path} ({out_path.stat().st_size} bytes)")
    montage = Image.new("RGB", (120 * 7, 160), "black")
    for number, preview in enumerate(previews):
        montage.paste(preview, (number * 120, 0))
    montage_path = PREVIEW_DIR / "listen_montage.png"
    montage.save(montage_path)
    print(f"preview: {montage_path}")


if __name__ == "__main__":
    main()
