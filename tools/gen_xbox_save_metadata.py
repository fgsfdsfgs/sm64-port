#!/usr/bin/env python3
"""Generate retail-dashboard title/save images from ROM-extracted SM64 HUD assets.

Title source:
  segment2.05A00.rgba16  -> HUD Mario head

Save source:
  segment2.05C00.rgba16  -> HUD gold star

Outputs are embedded into a generated C translation unit in build/:
- TitleImage.xbx: 128x128 Windows BMP, 24 bpp
- SaveImage.xbx: 64x64 Xbox XPR0, DXT1

The Nintendo-derived image bytes remain local build artifacts only.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

SOURCE_WIDTH = 16
SOURCE_HEIGHT = 16
SOURCE_BYTES = SOURCE_WIDTH * SOURCE_HEIGHT * 2

TITLE_WIDTH = 128
TITLE_HEIGHT = 128
SAVE_WIDTH = 64
SAVE_HEIGHT = 64

# Directly observed in the supplied retail Halo 2 save's SaveImage.xbx.
XPR_MAGIC = 0x30525058
XPR_TOTAL_SIZE = 4096
XPR_HEADER_SIZE = 2048
XPR_COMMON = 0x00040001
XPR_FORMAT_DXT1_64 = 0x06610C29
XPR_SIZE = 0x00000000

BMP_X_PPM = 2835
BMP_Y_PPM = 2835
TITLE_BACKGROUND = (0, 0, 0)


def expand5(v: int) -> int:
    return (v << 3) | (v >> 2)


def decode_n64_rgba16(raw: bytes) -> list[tuple[int, int, int, int]]:
    if len(raw) != SOURCE_BYTES:
        raise ValueError(f"expected {SOURCE_BYTES} source bytes, got {len(raw)}")

    pixels = []
    for i in range(0, len(raw), 2):
        value = (raw[i] << 8) | raw[i + 1]
        r = expand5((value >> 11) & 0x1F)
        g = expand5((value >> 6) & 0x1F)
        b = expand5((value >> 1) & 0x1F)
        a = 0xFF if (value & 1) else 0x00
        pixels.append((r, g, b, a))
    return pixels


def upscale_nearest(pixels, dst_width: int, dst_height: int):
    if dst_width % SOURCE_WIDTH or dst_height % SOURCE_HEIGHT:
        raise ValueError("destination dimensions must be integer multiples of 16")

    sx = dst_width // SOURCE_WIDTH
    sy = dst_height // SOURCE_HEIGHT
    out = []

    for y in range(dst_height):
        src_y = y // sy
        row = src_y * SOURCE_WIDTH
        for x in range(dst_width):
            src_x = x // sx
            out.append(pixels[row + src_x])

    return out


def encode_title_bmp24(pixels) -> bytes:
    """Encode an exact 128x128, bottom-up, 24-bit Windows BMP."""
    width = TITLE_WIDTH
    height = TITLE_HEIGHT
    row_bytes = width * 3
    image_bytes = row_bytes * height
    file_size = 54 + image_bytes

    out = bytearray()
    out += b"BM"
    out += struct.pack("<IHHI", file_size, 0, 0, 54)
    out += struct.pack(
        "<IiiHHIIiiII",
        40,
        width,
        height,
        1,
        24,
        0,
        image_bytes,
        BMP_X_PPM,
        BMP_Y_PPM,
        0,
        0,
    )

    # Positive BMP height means bottom-up rows.
    for y in range(height - 1, -1, -1):
        row = y * width
        for x in range(width):
            r, g, b, a = pixels[row + x]
            if a == 0:
                r, g, b = TITLE_BACKGROUND
            out += bytes((b, g, r))

    if len(out) != file_size:
        raise AssertionError(f"BMP size mismatch: {len(out)} != {file_size}")

    return bytes(out)


def rgb888_to_565(rgb: tuple[int, int, int]) -> int:
    r, g, b = rgb
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)


def rgb565_to_888(c: int) -> tuple[int, int, int]:
    r = (c >> 11) & 31
    g = (c >> 5) & 63
    b = c & 31
    return (
        (r * 255 + 15) // 31,
        (g * 255 + 31) // 63,
        (b * 255 + 15) // 31,
    )


def color_distance_sq(a, b) -> int:
    dr = int(a[0]) - int(b[0])
    dg = int(a[1]) - int(b[1])
    db = int(a[2]) - int(b[2])
    return dr * dr + dg * dg + db * db


def choose_farthest_endpoints(colors: list[tuple[int, int, int]]) -> tuple[int, int]:
    if not colors:
        return 0, 0

    unique = list(dict.fromkeys(colors))
    if len(unique) == 1:
        c = rgb888_to_565(unique[0])
        return c, c

    best = (0, 1)
    best_d = -1
    for i in range(len(unique)):
        for j in range(i + 1, len(unique)):
            d = color_distance_sq(unique[i], unique[j])
            if d > best_d:
                best_d = d
                best = (i, j)

    return rgb888_to_565(unique[best[0]]), rgb888_to_565(unique[best[1]])


def build_dxt1_palette(c0: int, c1: int):
    p0 = rgb565_to_888(c0)
    p1 = rgb565_to_888(c1)

    if c0 > c1:
        p2 = tuple((2 * p0[i] + p1[i]) // 3 for i in range(3))
        p3 = tuple((p0[i] + 2 * p1[i]) // 3 for i in range(3))
        return [
            (*p0, 255),
            (*p1, 255),
            (*p2, 255),
            (*p3, 255),
        ]

    p2 = tuple((p0[i] + p1[i]) // 2 for i in range(3))
    return [
        (*p0, 255),
        (*p1, 255),
        (*p2, 255),
        (0, 0, 0, 0),
    ]


def encode_dxt1_block(block: list[tuple[int, int, int, int]]) -> bytes:
    if len(block) != 16:
        raise ValueError("DXT1 block must contain 16 pixels")

    has_transparency = any(p[3] < 128 for p in block)
    opaque_rgb = [(p[0], p[1], p[2]) for p in block if p[3] >= 128]

    if not opaque_rgb:
        c0 = c1 = 0
        bits = 0xFFFFFFFF
        return struct.pack("<HHI", c0, c1, bits)

    e0, e1 = choose_farthest_endpoints(opaque_rgb)

    if has_transparency:
        c0, c1 = sorted((e0, e1))
        if c0 == c1:
            if c1 < 0xFFFF:
                c1 += 1
            elif c0 > 0:
                c0 -= 1
        if c0 > c1:
            c0, c1 = c1, c0
    else:
        c0, c1 = sorted((e0, e1), reverse=True)
        if c0 == c1:
            if c0 < 0xFFFF:
                c0 += 1
            elif c1 > 0:
                c1 -= 1
        if c0 <= c1:
            c0, c1 = max(c0, c1), min(c0, c1)
            if c0 == c1:
                c0 = min(0xFFFF, c0 + 1)

    palette = build_dxt1_palette(c0, c1)
    usable = 3 if has_transparency else 4

    bits = 0
    for i, p in enumerate(block):
        if has_transparency and p[3] < 128:
            code = 3
        else:
            rgb = (p[0], p[1], p[2])
            code = min(
                range(usable),
                key=lambda k: color_distance_sq(rgb, palette[k][:3]),
            )
        bits |= (code & 3) << (2 * i)

    return struct.pack("<HHI", c0, c1, bits)


def encode_dxt1(pixels, width: int, height: int) -> bytes:
    if width % 4 or height % 4:
        raise ValueError("DXT1 dimensions must be multiples of 4")

    out = bytearray()
    for by in range(0, height, 4):
        for bx in range(0, width, 4):
            block = []
            for py in range(4):
                row = (by + py) * width
                for px in range(4):
                    block.append(pixels[row + bx + px])
            out += encode_dxt1_block(block)

    expected = width * height // 2
    if len(out) != expected:
        raise AssertionError(f"DXT1 size mismatch: {len(out)} != {expected}")

    return bytes(out)


def encode_save_xpr0_dxt1(pixels) -> bytes:
    dxt = encode_dxt1(pixels, SAVE_WIDTH, SAVE_HEIGHT)
    if len(dxt) != 2048:
        raise AssertionError("64x64 DXT1 payload must be exactly 2048 bytes")

    descriptor = struct.pack(
        "<IIIII",
        XPR_COMMON,
        0,
        0,
        XPR_FORMAT_DXT1_64,
        XPR_SIZE,
    )

    out = bytearray()
    out += struct.pack("<III", XPR_MAGIC, XPR_TOTAL_SIZE, XPR_HEADER_SIZE)
    out += descriptor
    out += b"\xAD" * (XPR_HEADER_SIZE - len(out))
    out += dxt

    if len(out) != XPR_TOTAL_SIZE:
        raise AssertionError(f"XPR0 total size mismatch: {len(out)}")

    return bytes(out)


def c_array(name: str, data: bytes) -> str:
    lines = [f"const unsigned char {name}[] = {{"]
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    lines.append(f"const unsigned int {name}Size = sizeof({name});")
    return "\n".join(lines)


def generate_c(title: bytes, save: bytes, title_sha: str, save_sha: str) -> str:
    return (
        "/* Auto-generated from ROM-extracted SM64 HUD assets. */\n"
        "/* Do not commit this generated file or Nintendo-derived image bytes. */\n"
        f"/* Mario-head source SHA-256: {title_sha} */\n"
        f"/* HUD-star source SHA-256: {save_sha} */\n\n"
        + c_array("gXboxTitleImageXbx", title)
        + "\n\n"
        + c_array("gXboxSaveImageXbx", save)
        + "\n"
    )


def inspect_bmp(blob: bytes):
    if blob[:2] != b"BM":
        raise ValueError("BMP signature mismatch")
    size = struct.unpack_from("<I", blob, 2)[0]
    off = struct.unpack_from("<I", blob, 10)[0]
    dib = struct.unpack_from("<I", blob, 14)[0]
    width, height, planes, bpp = struct.unpack_from("<iiHH", blob, 18)
    return {
        "file_size": size,
        "pixel_offset": off,
        "dib_header_size": dib,
        "width": width,
        "height": height,
        "planes": planes,
        "bits_per_pixel": bpp,
    }


def inspect_xpr(blob: bytes):
    magic, total, header = struct.unpack_from("<III", blob, 0)
    common, data, lock, fmt, size = struct.unpack_from("<IIIII", blob, 12)
    return {
        "magic": f"0x{magic:08X}",
        "total_size": total,
        "header_size": header,
        "common": f"0x{common:08X}",
        "data": data,
        "lock": lock,
        "format": f"0x{fmt:08X}",
        "size": f"0x{size:08X}",
    }


def self_test() -> None:
    raw = bytearray()
    for y in range(SOURCE_HEIGHT):
        for x in range(SOURCE_WIDTH):
            r = x & 0x1F
            g = y & 0x1F
            b = (x + y) & 0x1F
            a = 1 if 2 <= x <= 13 and 2 <= y <= 13 else 0
            value = (r << 11) | (g << 6) | (b << 1) | a
            raw += bytes(((value >> 8) & 0xFF, value & 0xFF))

    decoded = decode_n64_rgba16(bytes(raw))
    title = encode_title_bmp24(
        upscale_nearest(decoded, TITLE_WIDTH, TITLE_HEIGHT)
    )
    save = encode_save_xpr0_dxt1(
        upscale_nearest(decoded, SAVE_WIDTH, SAVE_HEIGHT)
    )

    bi = inspect_bmp(title)
    assert bi == {
        "file_size": 49206,
        "pixel_offset": 54,
        "dib_header_size": 40,
        "width": 128,
        "height": 128,
        "planes": 1,
        "bits_per_pixel": 24,
    }

    xi = inspect_xpr(save)
    assert xi["magic"] == "0x30525058"
    assert xi["total_size"] == 4096
    assert xi["header_size"] == 2048
    assert xi["common"] == "0x00040001"
    assert xi["format"] == "0x06610C29"
    assert xi["size"] == "0x00000000"

    print("SM64_XBOX_DUAL_LOGO_GENERATOR_SELF_TEST_PASS")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--title-input", type=Path)
    parser.add_argument("--save-input", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        self_test()
        return 0

    if not args.title_input or not args.save_input or not args.output:
        parser.error(
            "--title-input, --save-input, and --output are required "
            "unless --self-test is used"
        )

    title_raw = args.title_input.read_bytes()
    save_raw = args.save_input.read_bytes()

    title_source_sha = hashlib.sha256(title_raw).hexdigest().upper()
    save_source_sha = hashlib.sha256(save_raw).hexdigest().upper()

    title_decoded = decode_n64_rgba16(title_raw)
    save_decoded = decode_n64_rgba16(save_raw)

    title = encode_title_bmp24(
        upscale_nearest(title_decoded, TITLE_WIDTH, TITLE_HEIGHT)
    )
    save = encode_save_xpr0_dxt1(
        upscale_nearest(save_decoded, SAVE_WIDTH, SAVE_HEIGHT)
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(
        generate_c(
            title,
            save,
            title_source_sha,
            save_source_sha,
        ).replace("\r\n", "\n").encode("utf-8")
    )

    report = {
        "schema": "sm64-xbox-dual-logo-generator-v1",
        "title_source": str(args.title_input),
        "title_source_role": "HUD_MARIO_HEAD",
        "title_source_bytes": len(title_raw),
        "title_source_sha256": title_source_sha,
        "save_source": str(args.save_input),
        "save_source_role": "HUD_GOLD_STAR",
        "save_source_bytes": len(save_raw),
        "save_source_sha256": save_source_sha,
        "source_format": "N64_RGBA16_RGBA5551_BIG_ENDIAN",
        "source_dimensions": [SOURCE_WIDTH, SOURCE_HEIGHT],
        "title_image_filename": "TitleImage.xbx",
        "title_image_format": "BMP24_BOTTOM_UP",
        "title_image_dimensions": [TITLE_WIDTH, TITLE_HEIGHT],
        "title_image_bytes": len(title),
        "title_image_sha256": hashlib.sha256(title).hexdigest().upper(),
        "title_image_header": inspect_bmp(title),
        "save_image_filename": "SaveImage.xbx",
        "save_image_format": "XPR0_DXT1",
        "save_image_dimensions": [SAVE_WIDTH, SAVE_HEIGHT],
        "save_image_bytes": len(save),
        "save_image_sha256": hashlib.sha256(save).hexdigest().upper(),
        "save_image_header": inspect_xpr(save),
        "generated_c": str(args.output),
        "generated_nintendo_bytes_committed": False,
    }

    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_bytes(
            (json.dumps(report, indent=2) + "\n")
            .replace("\r\n", "\n")
            .encode("utf-8")
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
