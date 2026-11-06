"""clearC 标识生成器：设计参数集中在这里，改完重跑即可再生成 assets/ 下的产物。

    python tools/gen_logo.py

产物: assets/logo.svg (矢量母版) / assets/logo.png (512) / assets/clearC.ico (多尺寸)
"""

import math
import struct
from io import BytesIO
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ASSETS = Path(__file__).resolve().parent.parent / "assets"

BG_TOP = (18, 197, 156)      # 渐变起点（左上）#12C59C
BG_BOTTOM = (10, 110, 209)   # 渐变终点（右下）#0A6ED1
RING = (255, 255, 255)       # C 字形
SHINE = (255, 216, 77)       # 清洁光点

CORNER = 0.225               # 圆角半径 / 边长
R_MID = 0.245                # C 弧形中线半径 / 边长
GAP_DEG = 36.0               # C 开口半角（0° 在 3 点钟方向）
STAR_IN = 0.34               # 四角星内径 / 外径
MAIN_SHINE = (0.755, 0.5, 0.118)                     # 主光点：位置 + 外径
SIDE_SHINE = ((0.875, 0.315, 0.062), (0.860, 0.700, 0.055))

ICO_SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)


def glyph_params(px):
    """按目标尺寸收敛笔画与装饰：≤40px 时四角星会糊成一团，改用实心圆点并砍掉侧光点。"""
    if px <= 24:
        return {"stroke": 0.152, "r_mid": 0.235, "dot": 0.080, "sides": ()}
    if px <= 40:
        return {"stroke": 0.132, "r_mid": 0.240, "dot": 0.082, "sides": SIDE_SHINE[:1]}
    if px <= 64:
        return {"stroke": 0.122, "r_mid": 0.245, "dot": None, "sides": SIDE_SHINE}
    return {"stroke": 0.118, "r_mid": 0.245, "dot": None, "sides": SIDE_SHINE}


def gradient(size):
    ax = np.linspace(0.0, 1.0, size)
    t = (ax[None, :] + ax[:, None]) * 0.5
    t = np.clip(t, 0.0, 1.0)
    lo = np.array(BG_TOP, dtype=np.float64)
    hi = np.array(BG_BOTTOM, dtype=np.float64)
    return (lo + (hi - lo) * t[..., None]).astype(np.uint8)


def tile_alpha(size):
    layer = Image.new("L", (size, size), 0)
    ImageDraw.Draw(layer).rounded_rectangle(
        [0, 0, size - 1, size - 1], radius=CORNER * size, fill=255)
    return np.asarray(layer, dtype=np.uint8)


def ring_mask(size, r_mid, stroke):
    layer = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(layer)
    r = r_mid * size
    w = max(1, round(stroke * size))
    c = size / 2.0
    d.arc([c - r, c - r, c + r, c + r], GAP_DEG, 360.0 - GAP_DEG, fill=255, width=w)
    for ang in (GAP_DEG, -GAP_DEG):
        x = c + r * math.cos(math.radians(ang))
        y = c + r * math.sin(math.radians(ang))
        d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=255)
    return np.asarray(layer, dtype=np.uint8)


def star_mask(size, cx, cy, r_out):
    layer = Image.new("L", (size, size), 0)
    ImageDraw.Draw(layer).polygon(star_points(size, cx, cy, r_out), fill=255)
    return np.asarray(layer, dtype=np.uint8)


def dot_mask(size, cx, cy, r):
    layer = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(layer)
    d.ellipse([cx * size - r * size, cy * size - r * size,
               cx * size + r * size, cy * size + r * size], fill=255)
    return np.asarray(layer, dtype=np.uint8)


def star_points(size, cx, cy, r_out, rot_deg=-90.0):
    r_in = r_out * STAR_IN
    pts = []
    for k in range(8):
        ang = math.radians(rot_deg + k * 45.0)
        r = r_out if k % 2 == 0 else r_in
        pts.append(((cx + r * math.cos(ang)) * size, (cy + r * math.sin(ang)) * size))
    return pts


def overlay(base, mask, rgba):
    layer = Image.new("RGBA", base.size, rgba)
    layer.putalpha(Image.frombytes("L", base.size, mask.tobytes()))
    base.alpha_composite(layer)
    return base


def render_mark(px):
    ss = 8 if px <= 64 else 4
    s = px * ss
    g = glyph_params(px)

    arr = np.dstack([gradient(s), tile_alpha(s)])
    img = Image.frombytes("RGBA", (s, s), arr.tobytes())

    img = overlay(img, ring_mask(s, g["r_mid"], g["stroke"]), RING + (255,))

    cx, cy, r_out = MAIN_SHINE
    if g["dot"] is None:
        img = overlay(img, star_mask(s, cx, cy, r_out), SHINE + (255,))
    else:
        img = overlay(img, dot_mask(s, cx, cy, g["dot"]), SHINE + (255,))
    for sx, sy, sr in g["sides"]:
        img = overlay(img, dot_mask(s, sx, sy, sr), SHINE + (255,))

    return img.resize((px, px), Image.LANCZOS)


def svg_path(points):
    return "M " + " L ".join(f"{x:.2f} {y:.2f}" for x, y in points) + " Z"


def arc_path(size, r_mid, stroke):
    r = r_mid * size
    c = size / 2.0
    p1 = (c + r * math.cos(math.radians(GAP_DEG)), c + r * math.sin(math.radians(GAP_DEG)))
    p2 = (c + r * math.cos(math.radians(-GAP_DEG)), c + r * math.sin(math.radians(-GAP_DEG)))
    d = (f"M {p1[0]:.2f} {p1[1]:.2f} A {r:.2f} {r:.2f} 0 1 1 {p2[0]:.2f} {p2[1]:.2f}")
    return d, stroke * size


def write_svg(path, size=256):
    g = glyph_params(size)
    d, w = arc_path(size, g["r_mid"], g["stroke"])
    cx, cy, r_out = MAIN_SHINE
    body = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" '
        f'viewBox="0 0 {size} {size}" role="img" aria-label="clearC">',
        "  <defs>",
        '    <linearGradient id="clearC-bg" x1="0" y1="0" x2="1" y2="1">',
        f'      <stop offset="0" stop-color="#{BG_TOP[0]:02x}{BG_TOP[1]:02x}{BG_TOP[2]:02x}"/>',
        f'      <stop offset="1" stop-color="#{BG_BOTTOM[0]:02x}{BG_BOTTOM[1]:02x}{BG_BOTTOM[2]:02x}"/>',
        "    </linearGradient>",
        "  </defs>",
        f'  <rect width="{size}" height="{size}" rx="{CORNER * size:.2f}" fill="url(#clearC-bg)"/>',
        f'  <path d="{d}" fill="none" stroke="#ffffff" stroke-width="{w:.2f}" stroke-linecap="round"/>',
        f'  <path d="{svg_path(star_points(size, cx, cy, r_out))}" fill="#ffd84d"/>',
    ]
    for sx, sy, sr in g["sides"]:
        body.append(f'  <circle cx="{sx * size:.2f}" cy="{sy * size:.2f}" r="{sr * size:.2f}" fill="#ffd84d"/>')
    body.append("</svg>")
    path.write_text("\n".join(body) + "\n", encoding="utf-8")


def dib_entry(img):
    w, h = img.size
    raw = img.tobytes()
    rows = bytearray()
    for y in range(h - 1, -1, -1):
        seg = bytearray(raw[y * w * 4:(y + 1) * w * 4])
        seg[0::4], seg[2::4] = seg[2::4], seg[0::4]
        rows += seg
    mask_row = ((w + 31) // 32) * 4
    and_mask = b"\x00" * (mask_row * h)
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, len(rows), 0, 0, 0, 0)
    return header + bytes(rows) + and_mask


def png_entry(img):
    buf = BytesIO()
    img.save(buf, format="PNG", optimize=True)
    return buf.getvalue()


def write_ico(path):
    entries = []
    for px in ICO_SIZES:
        img = render_mark(px)
        data = png_entry(img) if px >= 64 else dib_entry(img)
        entries.append((px, data))

    dir_size = 6 + 16 * len(entries)
    offset = dir_size
    icon_dir = struct.pack("<HHH", 0, 1, len(entries))
    images = b""
    for px, data in entries:
        dim = 0 if px >= 256 else px
        icon_dir += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
        images += data
    path.write_bytes(icon_dir + images)


def main():
    ASSETS.mkdir(parents=True, exist_ok=True)
    render_mark(512).save(ASSETS / "logo.png", optimize=True)
    write_svg(ASSETS / "logo.svg")
    write_ico(ASSETS / "clearC.ico")
    print(f"已生成 {ASSETS / 'logo.png'}")
    print(f"已生成 {ASSETS / 'logo.svg'}")
    print(f"已生成 {ASSETS / 'clearC.ico'} ({(ASSETS / 'clearC.ico').stat().st_size} 字节, "
          f"尺寸 {'/'.join(str(s) for s in ICO_SIZES)})")


if __name__ == "__main__":
    main()
