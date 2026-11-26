#!/usr/bin/env python3
"""Builds the pictures in docs/MATERIALS.md from the committed goldens.

Nothing is rendered here: the goldens are the pictures, so re-bless them
first (docs/GOLDEN_IMAGES.md) and then run this. Writes:

    docs/media/materials-goldens.png   the six ABI 1.20 goldens, captioned
    docs/media/tonemap-split.png       mesh_static_morning (default output)
                                       and output_srgb_aces (sRGB + ACES,
                                       exposure 1.4) cut at the middle, the
                                       way a before/after slider shows them

Needs Pillow (pip install pillow).

    tools/make_materials_media.py
"""
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
GOLDEN = os.path.join(ROOT, "tests", "golden")
MEDIA = os.path.join(ROOT, "docs", "media")
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

CASES = [
    ("mat_cutout_shadows", "Alpha cutout", "holes in the cards and in their shadows"),
    ("mat_emissive_night", "Emission", "glowing windows and sign; light only from the lamp"),
    ("mat_normal_map", "Normal map", "no mesh tangents; mirrored uvs; scale 0 is flat"),
    ("light_spot_cones", "Spot lights", "hard, soft and narrow cones; a shadowed spot"),
    ("cam_ortho_shadows", "Orthographic camera", "cascades fitted to the ortho box"),
    ("output_srgb_aces", "sRGB + ACES output", "exposure 1.4, filmic roll-off"),
]


def font(size, bold=False):
    try:
        return ImageFont.truetype(FONT_BOLD if bold else FONT, size)
    except OSError:
        return ImageFont.load_default()


def golden(name):
    return Image.open(os.path.join(GOLDEN, name, "linux-llvmpipe.png")).convert("RGB")


def captioned(img, title, sub):
    band = 52
    out = Image.new("RGB", (img.width, img.height + band), (24, 26, 30))
    out.paste(img, (0, 0))
    d = ImageDraw.Draw(out)
    d.text((12, img.height + 7), title, font=font(17, True), fill=(240, 240, 240))
    d.text((12, img.height + 29), sub, font=font(14), fill=(175, 180, 190))
    return out


def tag(d, xy, text, anchor):
    f = font(16, True)
    box = d.textbbox(xy, text, font=f, anchor=anchor)
    d.rounded_rectangle((box[0] - 8, box[1] - 5, box[2] + 8, box[3] + 5), 6, fill=(0, 0, 0))
    d.text(xy, text, font=f, fill=(255, 255, 255), anchor=anchor)


def main():
    os.makedirs(MEDIA, exist_ok=True)

    cells = [captioned(golden(n).resize((400, 225), Image.LANCZOS), t, s) for n, t, s in CASES]
    gap, (w, h) = 8, cells[0].size
    sheet = Image.new("RGB", (3 * w + 4 * gap, 2 * h + 3 * gap), (14, 15, 18))
    for i, c in enumerate(cells):
        sheet.paste(c, (gap + (i % 3) * (w + gap), gap + (i // 3) * (h + gap)))
    out = os.path.join(MEDIA, "materials-goldens.png")
    sheet.save(out, optimize=True)
    print("wrote %s (%d KiB)" % (os.path.relpath(out, ROOT), os.path.getsize(out) // 1024))

    before, after = golden("mesh_static_morning"), golden("output_srgb_aces")
    assert before.size == after.size
    split = before.copy()
    mid = split.width // 2
    split.paste(after.crop((mid, 0, split.width, split.height)), (mid, 0))
    d = ImageDraw.Draw(split)
    d.rectangle((mid - 1, 0, mid + 1, split.height), fill=(255, 255, 255))
    cy = split.height // 2
    d.ellipse((mid - 16, cy - 16, mid + 16, cy + 16), fill=(255, 255, 255), outline=(30, 30, 30), width=2)
    d.polygon([(mid - 4, cy - 7), (mid - 11, cy), (mid - 4, cy + 7)], fill=(30, 30, 30))
    d.polygon([(mid + 4, cy - 7), (mid + 11, cy), (mid + 4, cy + 7)], fill=(30, 30, 30))
    tag(d, (14, split.height - 14), "default output", "ls")
    tag(d, (split.width - 14, split.height - 14), "sRGB + ACES, exposure 1.4", "rs")
    out = os.path.join(MEDIA, "tonemap-split.png")
    split.save(out, optimize=True)
    print("wrote %s (%d KiB)" % (os.path.relpath(out, ROOT), os.path.getsize(out) // 1024))


if __name__ == "__main__":
    main()
