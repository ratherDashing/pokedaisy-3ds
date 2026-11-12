#!/usr/bin/env python3
"""The .cia's HOME Menu banner: ctr/banner.png (256 x 128: the app's icon, enlarged). Needs Pillow. Its sound is
silence, made by ctr/Makefile.

    python3 3ds/tools/gen_banner.py
"""
import os

from PIL import Image

CTR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ctr")


def main():
    icon = Image.open(os.path.join(CTR, "icon.png")).convert("RGBA")
    banner = Image.new("RGBA", (256, 128), (0, 0, 0, 0))
    big = icon.resize((96, 96), Image.NEAREST)
    banner.paste(big, ((256 - 96) // 2, (128 - 96) // 2), big)
    banner.save(os.path.join(CTR, "banner.png"))


if __name__ == "__main__":
    main()
