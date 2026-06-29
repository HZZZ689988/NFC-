from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw, ImageFont, ImageOps


PORTRAIT_SIZE = (48, 64)
TEXT_SIZE = (80, 16)


@dataclass(frozen=True)
class ImageBlocks:
    portrait: list[bytes]
    name: list[bytes]
    department: list[bytes]


def _to_1bit_bytes(image: Image.Image) -> bytes:
    mono = image.convert("1")
    raw = mono.tobytes()
    return bytes((~byte) & 0xFF for byte in raw)


def _split_blocks(data: bytes, expected_blocks: int) -> list[bytes]:
    if len(data) != expected_blocks * 16:
        raise ValueError(f"数据长度错误，应为 {expected_blocks * 16} 字节")
    return [data[i : i + 16] for i in range(0, len(data), 16)]


def load_portrait(path: str | Path, threshold: int = 150) -> Image.Image:
    source = Image.open(path)
    image = ImageOps.exif_transpose(source).convert("L")
    image.thumbnail(PORTRAIT_SIZE, Image.Resampling.LANCZOS)
    canvas = Image.new("L", PORTRAIT_SIZE, 255)
    x = (PORTRAIT_SIZE[0] - image.width) // 2
    y = (PORTRAIT_SIZE[1] - image.height) // 2
    canvas.paste(image, (x, y))
    return canvas.point(lambda value: 0 if value < threshold else 255, mode="1")


def _font_candidates() -> Iterable[str]:
    yield "C:/Windows/Fonts/msyh.ttc"
    yield "C:/Windows/Fonts/simhei.ttf"
    yield "C:/Windows/Fonts/simsun.ttc"
    yield "C:/Windows/Fonts/arial.ttf"


def load_text_font(text: str) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    for size in range(15, 7, -1):
        for candidate in _font_candidates():
            try:
                font = ImageFont.truetype(candidate, size)
            except OSError:
                continue
            bbox = font.getbbox(text or " ")
            if bbox[2] - bbox[0] <= TEXT_SIZE[0] - 2 and bbox[3] - bbox[1] <= TEXT_SIZE[1]:
                return font
    return ImageFont.load_default()


def render_text_bitmap(text: str) -> Image.Image:
    canvas = Image.new("L", TEXT_SIZE, 255)
    draw = ImageDraw.Draw(canvas)
    display = text.strip()
    font = load_text_font(display)
    bbox = draw.textbbox((0, 0), display or " ", font=font)
    width = bbox[2] - bbox[0]
    height = bbox[3] - bbox[1]
    x = max(0, (TEXT_SIZE[0] - width) // 2 - bbox[0])
    y = max(0, (TEXT_SIZE[1] - height) // 2 - bbox[1])
    draw.text((x, y), display, fill=0, font=font)
    return canvas.convert("1")


def portrait_blocks(image: Image.Image) -> list[bytes]:
    image = image.resize(PORTRAIT_SIZE).convert("1")
    return _split_blocks(_to_1bit_bytes(image), 24)


def text_blocks(image: Image.Image) -> list[bytes]:
    image = image.resize(TEXT_SIZE).convert("1")
    return _split_blocks(_to_1bit_bytes(image), 10)


def build_image_blocks(
    portrait_path: str | Path | None,
    name: str,
    department: str,
    threshold: int = 150,
) -> ImageBlocks:
    if portrait_path:
        portrait = load_portrait(portrait_path, threshold)
    else:
        portrait = Image.new("1", PORTRAIT_SIZE, 255)
    return ImageBlocks(
        portrait=portrait_blocks(portrait),
        name=text_blocks(render_text_bitmap(name)),
        department=text_blocks(render_text_bitmap(department)),
    )


def preview_image(image: Image.Image, scale: int = 4) -> Image.Image:
    width, height = image.size
    return image.resize((width * scale, height * scale), Image.Resampling.NEAREST)
