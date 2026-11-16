"""
FOREST's panel in Sega Rally 2's course selection (srally2_forest.py): its
photo and its title, as 16-bit texels (0RRRRRGGGGGBBBBB), rows from the
texture's first (the image's bottom row: the panel maps them so).

- The photo: a frame of a race on FOREST in Supermodel (Src/Model3/
  Srally2ForestPhoto.h, 256x128, rows from the top), or an image file given.
- The title: FOREST and HIDDEN in a stroke font of the patch's, drawn as the
  game's titles are (a neon tube: a white core, the colour around it, a
  glow) over dark streaks, 256x64.
"""

import math
import os
import re

import numpy as np

PHOTO_W, PHOTO_H = 256, 128
TITLE_W, TITLE_H = 256, 64
PHOTO_HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Src", "Model3", "Srally2ForestPhoto.h")

# Letters: their width (of their height) and strokes (points in their box, 0..1, y down)
GLYPHS = {
    "F": (0.72, [[(0, 0), (0, 1)], [(0, 0), (0.85, 0)], [(0, 0.48), (0.7, 0.48)]]),
    "E": (0.72, [[(0.85, 0), (0, 0), (0, 1), (0.85, 1)], [(0, 0.48), (0.7, 0.48)]]),
    "T": (0.72, [[(0, 0), (1, 0)], [(0.5, 0), (0.5, 1)]]),
    "O": (0.72, [[(0.15, 0), (0.85, 0), (1, 0.15), (1, 0.85), (0.85, 1), (0.15, 1), (0, 0.85), (0, 0.15), (0.15, 0)]]),
    "R": (0.72, [[(0, 1), (0, 0), (0.85, 0), (1, 0.12), (1, 0.36), (0.85, 0.48), (0, 0.48)], [(0.55, 0.48), (1, 1)]]),
    "S": (0.72, [[(0.97, 0.14), (0.85, 0), (0.15, 0), (0.03, 0.12), (0.03, 0.36), (0.15, 0.48), (0.85, 0.52), (0.97, 0.64),
                  (0.97, 0.88), (0.85, 1), (0.15, 1), (0.03, 0.86)]]),
    "H": (0.72, [[(0, 0), (0, 1)], [(1, 0), (1, 1)], [(0, 0.5), (1, 0.5)]]),
    "I": (0.35, [[(0.5, 0), (0.5, 1)]]),
    "D": (0.72, [[(0, 0), (0, 1), (0.8, 1), (1, 0.8), (1, 0.2), (0.8, 0), (0, 0)]]),
    "N": (0.72, [[(0, 1), (0, 0), (1, 1), (1, 0)]]),
    "A": (0.78, [[(0, 1), (0.5, 0), (1, 1)], [(0.22, 0.62), (0.78, 0.62)]]),
    "C": (0.72, [[(0.97, 0.15), (0.82, 0), (0.18, 0), (0, 0.18), (0, 0.82), (0.18, 1), (0.82, 1), (0.97, 0.85)]]),
    "U": (0.72, [[(0, 0), (0, 0.82), (0.18, 1), (0.82, 1), (1, 0.82), (1, 0)]]),
    "!": (0.3, [[(0.5, 0), (0.5, 0.68)], [(0.5, 0.93), (0.5, 0.97)]]),
    " ": (0.5, []),
}


def texel(r, g, b):
    c = lambda v: int(min(max(v, 0.0), 255.0) * 31.0 / 255.0 + 0.5)
    return c(r) << 10 | c(g) << 5 | c(b)


def photo_texels(path=None):
    """The photo's texels: an image file's (256x128, PIL), else the one built in"""
    if path:
        from PIL import Image
        img = Image.open(path).convert("RGB").resize((PHOTO_W, PHOTO_H))
        rows = [[texel(*img.getpixel((x, y))) for x in range(PHOTO_W)] for y in range(PHOTO_H)]
    else:
        text = open(PHOTO_HEADER, encoding="utf-8").read()
        body = text[text.index("{", text.index("kSrally2ForestPhoto")) + 1:text.index("};")]
        values = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{4})", body)]
        if len(values) != PHOTO_W * PHOTO_H:
            raise ValueError("%s: not a 256x128 photo" % PHOTO_HEADER)
        rows = [values[y * PHOTO_W:(y + 1) * PHOTO_W] for y in range(PHOTO_H)]
    return [t for row in reversed(rows) for t in row]


def _distance(w, h, text, x0, y0, height, gap, slant):
    """Each pixel's distance (centres) to the strokes of a text"""
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float64)
    cx, cy = xs + 0.5, ys + 0.5
    dist = np.full((h, w), 1e9)
    x = x0
    for ch in text:
        width, strokes = GLYPHS[ch]
        gw = height * width
        for stroke in strokes:
            for (ax_, ay_), (bx_, by_) in zip(stroke, stroke[1:]):
                ax = x + ax_ * gw + (1 - ay_) * height * slant
                ay = y0 + ay_ * height
                bx = x + bx_ * gw + (1 - by_) * height * slant
                by = y0 + by_ * height
                dx, dy = bx - ax, by - ay
                length = dx * dx + dy * dy
                t = np.clip(((cx - ax) * dx + (cy - ay) * dy) / length, 0, 1) if length > 0 else 0
                dist = np.minimum(dist, np.hypot(cx - ax - t * dx, cy - ay - t * dy))
        x += gw + gap
    return dist


def _blur(a, sigma):
    r = int(math.ceil(sigma * 3))
    k = np.exp(-np.arange(-r, r + 1) ** 2 / (2 * sigma * sigma))
    k /= k.sum()
    pad = np.pad(a, ((0, 0), (r, r)), mode="edge")
    a = sum(k[i] * pad[:, i:i + a.shape[1]] for i in range(2 * r + 1))
    pad = np.pad(a, ((r, r), (0, 0)), mode="edge")
    return sum(k[i] * pad[i:i + a.shape[0], :] for i in range(2 * r + 1))


def title_texels():
    w, h = TITLE_W, TITLE_H
    color = np.array([40.0, 230.0, 150.0])
    img = np.zeros((h, w, 3))
    seed = 7

    def random():
        nonlocal seed
        seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF
        return (seed >> 8) / float(1 << 24)

    for y in range(h):
        base = 10 + 30 * (1 - abs(y - 40) / 40.0)
        streak = random() ** 3 * 70
        phase = random() * 6
        v = base + streak * (0.6 + 0.4 * np.sin(np.arange(w) / w * 6 + phase))
        img[y, :, 0], img[y, :, 1], img[y, :, 2] = v * 0.55, v * 0.6, v * 0.8
    big = _distance(w, h, "FOREST", 22, 25, 30, 7, 0.18)
    small = _distance(w, h, "HIDDEN", 24, 11, 9, 2.5, 0.18)
    body = np.clip(3.9 - big, 0, 1)
    core = np.clip(1.9 - big, 0, 1)
    sub = np.clip(1.8 - small, 0, 1)
    glow = _blur(body, 3.0)
    g = np.minimum(glow * 1.6, 1)
    for c in range(3):
        v = img[:, :, c] * (1 - g) + glow * color[c] * 1.3
        v = v * (1 - body) + body * (color[c] * 0.75 + 255 * 0.25)
        v = v * (1 - core) + core * (255 * 0.85 + color[c] * 0.15)
        v = v * (1 - sub) + sub * (color[c] * 0.6 + 255 * 0.4)
        img[:, :, c] = v
    return [texel(*img[h - 1 - y, x]) for y in range(h) for x in range(w)]


def average_rgb5(*texels):
    """16-bit 0RRRRRGGGGGBBBBB texels averaged field by field"""
    return sum(((sum((t >> s) & 31 for t in texels) + 2) // 4) << s for s in (10, 5, 0))


# --- The title shown before FOREST's races (the game's are full-screen tile images: "SEGA RALLY 2", the course's
# name, a photo, "START!!"): the patch's own, as a Sega CGM image (srally2_forest.py makes the file)
SCREEN_W, SCREEN_H = 496, 384


def photo_rgb():
    """The photo, rows from the top, RGB floats (256x128)"""
    text = open(PHOTO_HEADER, encoding="utf-8").read()
    body = text[text.index("{", text.index("kSrally2ForestPhoto")) + 1:text.index("};")]
    v = np.array([int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{4})", body)]).reshape(PHOTO_H, PHOTO_W)
    return np.stack([((v >> 10) & 31) * 255 / 31, ((v >> 5) & 31) * 255 / 31, (v & 31) * 255 / 31], axis=2)


def _resize(img, w, h):
    """Bilinear"""
    sh, sw = img.shape[:2]
    ys = (np.arange(h) + 0.5) * sh / h - 0.5
    xs = (np.arange(w) + 0.5) * sw / w - 0.5
    y0 = np.clip(np.floor(ys).astype(int), 0, sh - 1); y1 = np.clip(y0 + 1, 0, sh - 1); fy = np.clip(ys - y0, 0, 1)
    x0 = np.clip(np.floor(xs).astype(int), 0, sw - 1); x1 = np.clip(x0 + 1, 0, sw - 1); fx = np.clip(xs - x0, 0, 1)
    top = img[y0][:, x0] * (1 - fx)[None, :, None] + img[y0][:, x1] * fx[None, :, None]
    bottom = img[y1][:, x0] * (1 - fx)[None, :, None] + img[y1][:, x1] * fx[None, :, None]
    return top * (1 - fy)[:, None, None] + bottom * fy[:, None, None]


def _ink(img, coverage, color):
    a = np.clip(coverage, 0, 1)[:, :, None]
    img[:] = img * (1 - a) + np.array(color, float) * a


def title_card_rgb():
    """The title card: white, the photo with soft edges, HIDDEN COURSE, FOREST in red underlined, START!! (rows from
    the top, RGB floats, 496x384)"""
    w, h = SCREEN_W, SCREEN_H
    img = np.full((h, w, 3), 250.0)
    # The photo, faded into the white at its edges
    pw, ph, px, py = 470, 250, 13, 92
    photo = _resize(photo_rgb(), pw, ph)
    ys, xs = np.mgrid[0:ph, 0:pw]
    edge = np.minimum(np.minimum(xs, pw - 1 - xs) / 50.0, np.minimum(ys, ph - 1 - ys) / 36.0)
    a = np.clip(edge, 0, 1) ** 1.5
    region = img[py:py + ph, px:px + pw]
    region[:] = region * (1 - a[:, :, None]) + photo * a[:, :, None]

    def text(s, x0, y0, height, gap, slant, thickness, color, shadow=None):
        d = _distance(w, h, s, x0, y0, height, gap, slant)
        if shadow:
            ds = _distance(w, h, s, x0 + shadow[0], y0 + shadow[1], height, gap, slant)
            _ink(img, (thickness + 1.2 - ds) * 0.8, shadow[2])
        _ink(img, thickness + 1.0 - d, color)

    text("HIDDEN COURSE", 52, 44, 10, 6.5, 0.0, 1.0, (110, 110, 110))
    text("FOREST", 50, 62, 40, 9, 0.22, 4.2, (225, 25, 20), (3, 3, (60, 20, 20)))
    line = np.zeros((h, w)); line[109:111, 48:470] = 1
    _ink(img, line, (225, 25, 20))
    text("START!!", 56, 262, 62, 12, 0.22, 6.5, (230, 20, 20), (4, 4, (70, 10, 10)))
    return img
