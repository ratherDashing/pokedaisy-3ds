#!/usr/bin/env python3
"""The .cia's HOME Menu banner: ctr/banner.png (256 x 128) and its chime, ctr/banner.wav. Needs Pillow.

The picture is the app's own daisy (the launcher icon's foreground, at its drawn pixel size) beside the name in the
app's pixel font (Pixel Operator), on a card of the launcher's blue. The chime is written here, note by note: two and a
half seconds of square waves over a triangle bass, a tune of this port's own (the HOME Menu plays at most three).

    python3 3ds/tools/gen_banner.py
"""
import math
import os
import re
import struct
import wave

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CTR = os.path.join(ROOT, "3ds", "ctr")
FG = os.path.join(ROOT, "app/src/main/res/mipmap-xxxhdpi/ic_launcher_foreground.png")
BG_XML = os.path.join(ROOT, "app/src/main/res/values/ic_launcher_background.xml")
FONT = os.path.join(ROOT, "app/src/main/assets/fonts/PixelOperator.ttf")

INK = (16, 34, 66, 255)
WHITE = (255, 255, 255, 255)
PALE = (214, 234, 255, 255)


def daisy(scale):
    """The launcher's flower at `scale` screen pixels to each of its drawn pixels."""
    fg = Image.open(FG).convert("RGBA")
    fg = fg.crop(fg.getbbox())
    # The art is pixel art enlarged: the shortest run of one colour along its middle row is one drawn pixel.
    row = [fg.getpixel((x, fg.height // 2)) for x in range(fg.width)]
    runs, n = [], 1
    for a, b in zip(row, row[1:]):
        if a == b:
            n += 1
        else:
            runs.append(n)
            n = 1
    unit = min(runs + [n])
    small = fg.resize((round(fg.width / unit), round(fg.height / unit)), Image.NEAREST)
    return small.resize((small.width * scale, small.height * scale), Image.NEAREST)


def text(s, size, colour, bold=False):
    """`s` in the pixel font with no smoothing, an ink outline round it; `size` a multiple of the font's 16."""
    font = ImageFont.truetype(FONT, 16)
    probe = ImageDraw.Draw(Image.new("L", (1, 1)))
    probe.fontmode = "1"
    box = probe.textbbox((0, 0), s, font=font)
    w, h = box[2] + 3 + bold, box[3] + 4
    glyphs = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(glyphs)
    d.fontmode = "1"
    for dx in range(1 + bold):
        d.text((1 + dx, 1), s, font=font, fill=255)
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ink = Image.new("RGBA", (w, h), INK)
    for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, -1), (-1, 1), (1, 1), (0, 2), (1, 2), (-1, 2)):
        out.paste(ink, (dx, dy), glyphs)
    out.paste(Image.new("RGBA", (w, h), colour), (0, 0), glyphs)
    k = size // 16
    out = out.resize((w * k, h * k), Image.NEAREST)
    return out.crop(out.getbbox())


def picture():
    with open(BG_XML) as f:
        rgb = re.search(r"#([0-9a-fA-F]{6})", f.read()).group(1)
    blue = tuple(int(rgb[i:i + 2], 16) for i in (0, 2, 4))
    banner = Image.new("RGBA", (256, 128), (0, 0, 0, 0))
    # The card: the launcher's blue, lighter at the top, an ink edge, a highlight line under the top edge.
    x0, y0, x1, y1 = 4, 22, 251, 105
    card = Image.new("RGBA", banner.size, (0, 0, 0, 0))
    for y in range(y0, y1 + 1):
        band = min(5, (y - y0) * 6 // (y1 - y0 + 1)) / 5  # six flat bands, as pixel art shades
        c = tuple(round(v + (255 - v) * 0.22 * (1 - band) - v * 0.20 * band) for v in blue)
        ImageDraw.Draw(card).line([(x0, y), (x1, y)], fill=c + (255,))
    mask = Image.new("L", banner.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((x0, y0, x1, y1), radius=10, fill=255)
    banner.paste(card, (0, 0), mask)
    d = ImageDraw.Draw(banner)
    d.rounded_rectangle((x0, y0, x1, y1), radius=10, outline=INK, width=2)
    d.line([(x0 + 10, y0 + 3), (x1 - 10, y0 + 3)], fill=(255, 255, 255, 90))

    # The flower stands taller than the card, as a banner's subject does.
    flower = daisy(3)
    fx = 12
    banner.alpha_composite(flower, (fx, (128 - flower.height) // 2))
    name = text("PokeDaisy", 32, WHITE, bold=True)
    line = text("GBA  GB  GBC", 16, PALE)
    left = fx + flower.width + 6
    room = x1 - 6 - left
    top = 64 - (name.height + 7 + line.height) // 2
    banner.alpha_composite(name, (left + (room - name.width) // 2, top))
    banner.alpha_composite(line, (left + (room - line.width) // 2, top + name.height + 7))
    banner.save(os.path.join(CTR, "banner.png"))
    print("wrote 3ds/ctr/banner.png: flower %dx%d, name %dx%d" % (flower.size + name.size))


RATE = 32000


def note(name):
    """The frequency of a note such as "F#5"."""
    step = {"C": -9, "D": -7, "E": -5, "F": -4, "G": -2, "A": 0, "B": 2}[name[0]] + name.count("#")
    return 440.0 * 2 ** (step / 12 + int(name[-1]) - 4)


def voice(buf, at, length, freq, shape, level, decay):
    """Adds one note to `buf`: a 25% pulse, a 50% square or a triangle, with a short attack and a falling level."""
    for i in range(int(length * RATE)):
        j = int(at * RATE) + i
        if j >= len(buf):
            break
        t = i / RATE
        phase = (t * freq) % 1.0
        if shape == "triangle":
            s = 4 * abs(phase - 0.5) - 1
        else:
            s = 1.0 if phase < (0.25 if shape == "pulse" else 0.5) else -1.0
        env = min(1.0, t / 0.004) * math.exp(-t * decay) * min(1.0, (length - t) / 0.02)
        buf[j] += s * level * env


def chime():
    total = 2.6
    lead = [0.0] * int(total * RATE)
    under = [0.0] * int(total * RATE)
    # D major: a rising run, a turn at the top, and the octave held.
    beat = 0.125
    tune = [("D5", 1), ("F#5", 1), ("A5", 1), ("D6", 2), ("B5", 1), ("A5", 1), ("F#5", 1), ("A5", 2), ("E6", 1),
            ("D6", 7)]
    at = 0.05
    for name, beats in tune:
        hold = beats * beat
        voice(lead, at, hold * (2.4 if beats > 2 else 1.0), note(name), "pulse", 0.30, 5.0 if beats > 2 else 9.0)
        at += hold
    # Harmony under the long notes, and the bass.
    for name, start, hold in (("A5", 0.05 + 3 * beat, 0.3), ("F#5", 0.05 + 8 * beat, 0.3), ("A5", 0.05 + 11 * beat, 1.3),
                              ("F#5", 0.05 + 11 * beat, 1.3)):
        voice(under, start, hold, note(name), "square", 0.10, 4.0)
    for name, start, hold in (("D3", 0.05, 0.5), ("G3", 0.05 + 4 * beat, 0.5), ("A3", 0.05 + 8 * beat, 0.375),
                              ("D3", 0.05 + 11 * beat, 1.3)):
        voice(under, start, hold, note(name), "triangle", 0.34, 2.5)
    # An echo of the lead, later on the right than on the left, widens it; the last 0.3 s fades to nothing.
    n = len(lead)
    mixed = []
    peak = 0.0
    for i in range(n):
        dl, dr = i - int(0.150 * RATE), i - int(0.225 * RATE)
        fade = min(1.0, (n - i) / (0.3 * RATE))
        l = (lead[i] + under[i] + (0.30 * lead[dl] if dl >= 0 else 0.0)) * fade
        r = (lead[i] + under[i] + (0.30 * lead[dr] if dr >= 0 else 0.0)) * fade
        mixed.append((l, r))
        peak = max(peak, abs(l), abs(r))
    gain = 0.5 / peak  # 6 dB under full scale: the HOME Menu's own sounds are quiet
    frames = bytearray()
    for l, r in mixed:
        frames += struct.pack("<hh", int(l * gain * 32767), int(r * gain * 32767))
    with wave.open(os.path.join(CTR, "banner.wav"), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(frames))
    print("wrote 3ds/ctr/banner.wav: %.2f s" % total)


if __name__ == "__main__":
    picture()
    chime()
