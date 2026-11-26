#!/usr/bin/env python3
"""Renders the pictures in docs/GLTF.md with strata-gltf-view.

Everything here is synthetic: the five test assets from
modules/gltf/tests/make_fixtures.py, and a block town of 145 meshes and 24
procedurally drawn textures that this script writes to a temporary
directory (it is not committed). Writes:

    docs/media/gltf-fixtures.png   the test assets in a labelled grid
    docs/media/gltf-budget.png     the block town as loaded, and squeezed
                                   into 4 meshes and 1 texture (merged and atlased)
    docs/media/gltf-orbit.gif      the block town, orbited

Needs a built strata-gltf-view and Pillow (pip install pillow); run headless
with LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe.

    tools/make_gltf_media.py build/strata-gltf-view
"""
import json
import math
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ASSETS = os.path.join(ROOT, "modules", "gltf", "tests", "assets")
MEDIA = os.path.join(ROOT, "docs", "media")
sys.path.insert(0, os.path.join(ROOT, "modules", "gltf", "tests"))
import make_fixtures as fx  # noqa: E402

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

# label, asset, strata-gltf-view arguments (as the goldens use them)
FIXTURES = [
    ("cube.gltf", "one box, one base colour", "cube.gltf", []),
    ("textured_quad.glb", "embedded PNG, nearest + clamp", "textured_quad.glb", ["--camera", "front", "--sun", "noon"]),
    ("hierarchy.gltf", "TRS + matrix nodes, mirrored wedge", "hierarchy.gltf", ["--sun", "morning"]),
    ("vertex_colors.glb", "COLOR_0 as float, u8 and u16", "vertex_colors.glb", ["--sun", "noon"]),
    ("alpha_modes.gltf", "OPAQUE / MASK / BLEND, unlit, emissive", "alpha_modes.gltf",
     ["--camera", "front", "--sun", "noon", "--zoom", "0.8"]),
    ("hierarchy.gltf, 1 mesh", "--max-meshes 1: merged, grid averaged", "hierarchy.gltf",
     ["--sun", "morning", "--max-meshes", "1"]),
]


def font(size, bold=False):
    try:
        return ImageFont.truetype(FONT_BOLD if bold else FONT, size)
    except OSError:
        return ImageFont.load_default()


def render(view, asset, png, args, size="640x360"):
    report = png + ".json"
    subprocess.run([view, asset, png, "--quiet", "--size", size, "--json", report] + args,
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with open(report) as f:
        return json.load(f)


def captioned(img, title, sub):
    """img with a two-line caption band underneath."""
    band = 52
    out = Image.new("RGB", (img.width, img.height + band), (24, 26, 30))
    out.paste(img, (0, 0))
    d = ImageDraw.Draw(out)
    d.text((12, img.height + 7), title, font=font(17, True), fill=(240, 240, 240))
    d.text((12, img.height + 29), sub, font=font(14), fill=(175, 180, 190))
    return out


def grid(cells, cols, gap=8):
    w, h = cells[0].size
    rows = (len(cells) + cols - 1) // cols
    out = Image.new("RGB", (cols * w + (cols + 1) * gap, rows * h + (rows + 1) * gap), (14, 15, 18))
    for i, c in enumerate(cells):
        out.paste(c, (gap + (i % cols) * (w + gap), gap + (i // cols) * (h + gap)))
    return out


# ------------------------------------------------------------------ block town

def facade(seed):
    """A 32x32 facade: a wall colour with a 3x3 grid of windows, all inside 0..1 uv."""
    rnd = (seed * 2654435761) & 0xFFFFFFFF
    hue = [(200, 120, 90), (190, 175, 140), (120, 140, 170), (170, 110, 120), (150, 165, 120), (210, 190, 110)][seed % 6]
    shade = 0.75 + 0.25 * ((rnd >> 8) % 100) / 100
    wall = tuple(int(c * shade) for c in hue) + (255,)
    glass = (60, 80, 110, 255) if seed % 3 else (230, 210, 140, 255)

    def pixel(x, y):
        cx, cy = x % 11, y % 11
        if 3 <= cx <= 8 and 3 <= cy <= 8 and x < 33 and y < 33:
            return glass
        return wall
    return fx.png(32, 32, pixel)


def block_town():
    """145 primitives (one mesh each), 24 textures; uvs inside 0..1 so all of them can be atlased."""
    g = fx.Gltf("strata docs: block town")
    s = g.sampler(fx.LINEAR, fx.LINEAR_MIPMAP_LINEAR, fx.CLAMP, fx.CLAMP)
    mats = []
    for i in range(24):
        t = g.texture(g.image_view(facade(i), "facade %d" % i), s)
        mats.append(g.material(name="facade %d" % i,
                               pbrMetallicRoughness={"baseColorTexture": {"index": t}, "metallicFactor": 0}))
    roof_cols = [[0.35, 0.2, 0.15, 1], [0.25, 0.27, 0.3, 1], [0.45, 0.3, 0.2, 1], [0.2, 0.3, 0.25, 1]]
    roofs = [g.material(name="roof %d" % i, pbrMetallicRoughness={"baseColorFactor": c, "metallicFactor": 0})
             for i, c in enumerate(roof_cols)]
    ground = g.material(name="ground", pbrMetallicRoughness={"baseColorFactor": [0.42, 0.45, 0.4, 1], "metallicFactor": 0})
    nodes = []
    n = 0
    for gx in range(-5, 5):
        for gz in range(-5, 5):
            if gx in (-1, 0) or gz in (-1, 0):
                continue  # two crossing streets
            h = 1.0 + ((gx * 7 + gz * 13) % 5) * 0.55 + (1.2 if abs(gx + 0.5) + abs(gz + 0.5) < 4 else 0)
            x, z = gx * 2.0 + 1.0, gz * 2.0 + 1.0
            body = fx.box(1.4, h, 1.4, (x, h / 2, z))
            roof = fx.box(1.55, 0.18, 1.55, (x, h + 0.09, z))
            nodes.append(g.node(name="house %d" % n, mesh=g.mesh("house %d" % n, [
                g.primitive(body[0], body[1], body[2], body[3], mats[n % 24])])))
            nodes.append(g.node(name="roof %d" % n, mesh=g.mesh("roof %d" % n, [
                g.primitive(roof[0], roof[1], None, roof[3], roofs[n % 4])])))
            n += 1
    for k in range(-4, 4):
        for gx, gz in ((k * 2.0 + 1.0, -1.0), (-1.0, k * 2.0 + 1.0)):
            if len(nodes) >= 191:
                break
            lamp = fx.box(0.12, 0.9, 0.12, (gx + 0.85, 0.45, gz + 0.85))
            nodes.append(g.node(name="post", mesh=g.mesh("post", [
                g.primitive(lamp[0], lamp[1], None, lamp[3], roofs[1])])))
    pos, nrm, uv, idx = fx.quad_xz(22, 0.0)
    nodes.append(g.node(name="ground", mesh=g.mesh("ground", [g.primitive(pos, nrm, None, idx, ground)])))
    g.scene(nodes)
    return g.glb_bytes()


def label_counts(img, title, r):
    def n(count, word):
        return "%d %s%s" % (count, word, "" if count == 1 else "s")
    detail = "%s, %s, %s" % (n(r["meshes_out"], "mesh").replace("meshs", "meshes"), n(r["textures_out"], "texture"),
                             "merged by material, " + n(r["atlas_pages"], "atlas page") if r["atlased"]
                             else "one mesh per primitive")
    return captioned(img, title, detail)


def main():
    view = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "strata-gltf-view")
    os.makedirs(MEDIA, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        # 1. The fixtures, labelled.
        cells = []
        for i, (title, sub, asset, args) in enumerate(FIXTURES):
            png = os.path.join(tmp, "fixture%d.png" % i)
            render(view, os.path.join(ASSETS, asset), png, args)
            cells.append(captioned(Image.open(png).convert("RGB").resize((400, 225), Image.LANCZOS), title, sub))
        out = os.path.join(MEDIA, "gltf-fixtures.png")
        grid(cells, 3).save(out, optimize=True)
        print("wrote %s (%d KiB)" % (os.path.relpath(out, ROOT), os.path.getsize(out) // 1024))

        # 2. The block town, as loaded and squeezed.
        town = os.path.join(tmp, "block_town.glb")
        with open(town, "wb") as f:
            f.write(block_town())
        cam = ["--eye", "17,13,19", "--target", "0,1.2,0", "--fov", "40", "--sun", "afternoon"]
        before_png, after_png = os.path.join(tmp, "before.png"), os.path.join(tmp, "after.png")
        before = render(view, town, before_png, cam)
        after = render(view, town, after_png, cam + ["--max-meshes", "4", "--max-textures", "1"])
        pair = [label_counts(Image.open(before_png).convert("RGB").resize((560, 315), Image.LANCZOS),
                             "As loaded (default budget)", before),
                label_counts(Image.open(after_png).convert("RGB").resize((560, 315), Image.LANCZOS),
                             "--max-meshes 4 --max-textures 1", after)]
        out = os.path.join(MEDIA, "gltf-budget.png")
        grid(pair, 2).save(out, optimize=True)
        print("wrote %s (%d KiB): %d primitives, %d textures in" % (
            os.path.relpath(out, ROOT), os.path.getsize(out) // 1024, before["primitives_in"], before["textures_in"]))

        # 3. An orbit of the block town.
        frames = []
        steps = 36
        for i in range(steps):
            a = 2 * math.pi * i / steps + 0.8
            eye = "%.3f,%.3f,%.3f" % (24 * math.cos(a), 12.0, 24 * math.sin(a))
            png = os.path.join(tmp, "orbit%02d.png" % i)
            render(view, town, png, ["--eye", eye, "--target", "0,1.2,0", "--fov", "40", "--sun", "afternoon"],
                   size="480x270")
            frames.append(Image.open(png).convert("RGB"))
        pal = [f.quantize(colors=128, method=Image.MEDIANCUT, dither=Image.NONE) for f in frames]
        out = os.path.join(MEDIA, "gltf-orbit.gif")
        pal[0].save(out, save_all=True, append_images=pal[1:], duration=90, loop=0, optimize=True)
        print("wrote %s (%d frames, %d KiB)" % (os.path.relpath(out, ROOT), len(frames), os.path.getsize(out) // 1024))


if __name__ == "__main__":
    main()
