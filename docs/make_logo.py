"""Draws logo.png (the mod icon): a cute GD cube jumping toward the mod's
closing-ring cue. Run: python docs/make_logo.py  (needs Pillow)."""
import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter

OUT = 336          # Geode logo size
S = OUT * 4        # draw big, then downsample for smooth edges
BLACK = (12, 12, 24, 255)
WHITE = (255, 255, 255, 255)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def rounded_mask(size, radius):
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    return m


def background():
    img = Image.new("RGBA", (S, S))
    d = ImageDraw.Draw(img)
    top, bottom = (64, 150, 255), (52, 66, 214)
    for y in range(S):
        d.line([(0, y), (S, y)], fill=lerp(top, bottom, y / S) + (255,))
    # GD-style background tiles
    tile = S // 6
    for i in range(1, 6):
        d.line([(i * tile, 0), (i * tile, S)], fill=(255, 255, 255, 11), width=5)
        d.line([(0, i * tile), (S, i * tile)], fill=(255, 255, 255, 11), width=5)
    # Ground with the glowing GD floor line
    ground = int(S * .76)
    d.rectangle([0, ground, S, S], fill=(30, 40, 150, 255))
    for i in range(1, 4):
        d.line([(i * tile * 1.5, ground), (i * tile * 1.5, S)], fill=(255, 255, 255, 22), width=6)
    glow = Image.new("RGBA", (S, S))
    ImageDraw.Draw(glow).line([(0, ground), (S, ground)], fill=(255, 255, 255, 200), width=30)
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(18)))
    d.line([(S * .12, ground), (S * .88, ground)], fill=WHITE, width=10)
    return img, ground


def spike(d, cx, ground, w, h):
    pts = [(cx - w / 2, ground), (cx + w / 2, ground), (cx, ground - h)]
    d.polygon(pts, fill=BLACK)
    inner = [(cx - w / 2 + 34, ground - 16), (cx + w / 2 - 34, ground - 16), (cx, ground - h + 52)]
    d.polygon(inner, fill=(40, 40, 70, 255))
    d.line(pts + [pts[0]], fill=WHITE, width=12, joint="curve")


def cube(size):
    """Default-colors GD cube (green + cyan) with a cute face."""
    c = Image.new("RGBA", (size, size))
    d = ImageDraw.Draw(c)
    o = size * .06
    d.rounded_rectangle([o, o, size - o, size - o], radius=size * .1, fill=BLACK)
    b = size * .12
    d.rounded_rectangle([b, b, size - b, size - b], radius=size * .07, fill=(125, 255, 0, 255))
    i = size * .27
    d.rounded_rectangle([i, i, size - i, size - i], radius=size * .05, fill=BLACK)
    j = size * .31
    d.rounded_rectangle([j, j, size - j, size - j], radius=size * .04, fill=(0, 230, 255, 255))
    # Face: big round eyes with shines, blush, little smile
    ey = size * .43
    for ex in (size * .41, size * .59):
        r = size * .07
        d.ellipse([ex - r, ey - r, ex + r, ey + r], fill=BLACK)
        s = size * .028
        d.ellipse([ex - r * .35 - s, ey - r * .4 - s, ex - r * .35 + s, ey - r * .4 + s], fill=WHITE)
    for bx in (size * .36, size * .64):
        d.ellipse([bx - size * .045, size * .53, bx + size * .045, size * .57], fill=(255, 120, 190, 210))
    d.arc([size * .43, size * .48, size * .57, size * .6], start=20, end=160, fill=BLACK, width=int(size * .025))
    return c


def ring_cue(img, cx, cy):
    """The mod's cue: white ring closing onto a green dot, with a glow."""
    glow = Image.new("RGBA", (S, S))
    ImageDraw.Draw(glow).ellipse([cx - 150, cy - 150, cx + 150, cy + 150], fill=(90, 255, 120, 120))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(50)))
    d = ImageDraw.Draw(img)
    r = 132
    d.ellipse([cx - r - 14, cy - r - 14, cx + r + 14, cy + r + 14], outline=BLACK, width=44)
    d.ellipse([cx - r - 6, cy - r - 6, cx + r + 6, cy + r + 6], outline=WHITE, width=26)
    dot = 38
    d.ellipse([cx - dot - 16, cy - dot - 16, cx + dot + 16, cy + dot + 16], fill=BLACK)
    d.ellipse([cx - dot - 8, cy - dot - 8, cx + dot + 8, cy + dot + 8], fill=WHITE)
    d.ellipse([cx - dot, cy - dot, cx + dot, cy + dot], fill=(70, 255, 100, 255))


def sparkle(d, x, y, r, color=WHITE):
    d.polygon([(x, y - r), (x + r * .28, y - r * .28), (x + r, y), (x + r * .28, y + r * .28),
               (x, y + r), (x - r * .28, y + r * .28), (x - r, y), (x - r * .28, y - r * .28)], fill=color)


def main():
    img, ground = background()
    d = ImageDraw.Draw(img)

    # Dotted jump arc from the cube to the cue
    start, end = (S * .47, S * .36), (S * .60, S * .33)
    for k in range(1, 5):
        t = k / 5
        x = start[0] + (end[0] - start[0]) * t
        y = start[1] + (end[1] - start[1]) * t - math.sin(t * math.pi) * S * .11
        rr = 15
        d.ellipse([x - rr - 6, y - rr - 6, x + rr + 6, y + rr + 6], fill=BLACK)
        d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=(255, 255, 255, 230))

    spike(d, S * .80, ground, 270, 300)
    ring_cue(img, int(S * .68), int(S * .36))

    # Cube, mid-jump and slightly rotated
    size = int(S * .40)
    c = cube(size).rotate(14, resample=Image.BICUBIC, expand=True)
    img.alpha_composite(c, (int(S * .07), int(ground - size * 1.22 - (c.height - size) / 2)))

    d = ImageDraw.Draw(img)
    sparkle(d, S * .88, S * .16, 46)
    sparkle(d, S * .36, S * .12, 30)
    sparkle(d, S * .93, S * .5, 26, (180, 255, 190, 255))

    # Rounded app-icon shape with a dark border
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    out.paste(img, (0, 0), rounded_mask(S, int(S * .2)))
    ImageDraw.Draw(out).rounded_rectangle([10, 10, S - 11, S - 11], radius=int(S * .2), outline=BLACK, width=26)

    path = Path(__file__).resolve().parent.parent / "logo.png"
    out.resize((OUT, OUT), Image.LANCZOS).save(path)
    print("saved", path)


if __name__ == "__main__":
    main()
