#!/usr/bin/env python3
"""Renders the Strata family picture at the top of docs/SHOWCASE.md.

Strata sits at the centre as a glowing core with a point light in it. Every
public project in docs/SHOWCASE.md is a body on a ring around it, one ring per
section: renders and previews inside, simulations and views in the middle,
games outside. A body's colour is its section and its size is its GitHub
repository size (log scale), as read from the GitHub API on the date in
SIZES_READ. Thin lines join each body to the core. Nothing is labelled in the
scene; the caption in SHOWCASE.md says what is where.

Drawn by Strata v0.7.0 itself through the in-tree Python binding, which
fetches the released library and checks it against its pinned SHA-256. Needs
Pillow (pip install pillow); run headless with LIBGL_ALWAYS_SOFTWARE=1
GALLIUM_DRIVER=llvmpipe.

    tools/make_family_media.py            # pinned sizes below
    tools/make_family_media.py --refresh  # print current sizes to paste in

Writes docs/media/showcase/family.jpg (the still) and docs/media/family-orbit.gif
(the camera going once round the core).
"""
import json
import math
import os
import random
import sys
import urllib.request

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(ROOT, "bindings", "python"))
import strata  # noqa: E402

STILL = os.path.join(ROOT, "docs", "media", "showcase", "family.jpg")
LOOP = os.path.join(ROOT, "docs", "media", "family-orbit.gif")
RELEASE = "0.7.0"

# The projects in docs/SHOWCASE.md, by section and in its order, with their
# repository size in KB.
SIZES_READ = "2026-10-29"
RINGS = [
    # (section, ring radius, colour)
    ("Renders and previews", 3.4, (0.60, 0.42, 1.00)),
    ("Simulations and views", 5.9, (0.30, 0.85, 0.78)),
    ("Games", 8.6, (1.00, 0.55, 0.38)),
]
PROJECTS = {
    "Games": [
        ("Preponderous-Software/beyond-nations", 75599),
        ("Preponderous-Software/steading", 1667),
        ("Preponderous-Software/kindling", 21457),
        ("Preponderous-Software/atman", 8200),
        ("Preponderous-Software/orrery", 29821),
        ("Preponderous-Software/sisyphus", 15771),
        ("Preponderous-Software/familiar", 125),
        ("Stephenson-Software/FishE", 2723),
    ],
    "Simulations and views": [
        ("Preponderous-Software/microbiome", 11053),
        ("Preponderous-Software/env-lib-cpp", 2824),
        ("Preponderous-Software/viron-strata", 774),
        ("Preponderous-Software/replay-player", 13782),
        ("Preponderous-Software/asn1-viz", 5826),
        ("Stephenson-Software/gardener", 5717),
        ("Preponderous-Software/playtester", 3672),
    ],
    "Renders and previews": [
        ("Dans-Plugins/dansplugins-dot-com", 16328),
        ("Dans-Plugins/Minecraft-USD-Exporter", 6821),
        ("Dans-Plugins/Seasons", 7113),
    ],
}

CORE_RADIUS = 1.1
CORE_COLOUR = (1.00, 0.86, 0.55)
EYE_HEIGHT = 7.5
EYE_DISTANCE = 15.5


def refresh():
    for section, repos in PROJECTS.items():
        print(f"    {section!r}: [")
        for repo, _ in repos:
            with urllib.request.urlopen(f"https://api.github.com/repos/{repo}") as r:
                data = json.load(r)
            print(f"        ({repo!r}, {data['size']}),")
        print("    ],")


def body_radius(size_kb):
    """0.19 for a 100 KB repository up to about 0.5 for 74 MB."""
    return 0.19 + 0.11 * max(math.log10(max(size_kb, 1)) - 2.0, 0.0)


def layout():
    """[(position, radius, colour)] for every project."""
    bodies = []
    for ring, (section, radius, colour) in enumerate(RINGS):
        repos = PROJECTS[section]
        start = 0.35 + ring * 0.9  # stagger the rings so the spokes do not line up
        for i, (_, size) in enumerate(repos):
            a = start + 2 * math.pi * i / len(repos)
            bodies.append(((radius * math.cos(a), 0.0, radius * math.sin(a)), body_radius(size), colour))
    return bodies


def uv_sphere(radius, stacks=24, slices=48):
    positions, normals, indices = [], [], []
    for i in range(stacks + 1):
        phi = math.pi * i / stacks
        for j in range(slices + 1):
            theta = 2 * math.pi * j / slices
            n = (math.sin(phi) * math.cos(theta), math.cos(phi), math.sin(phi) * math.sin(theta))
            normals += n
            positions += [radius * c for c in n]
    for i in range(stacks):
        for j in range(slices):
            a = i * (slices + 1) + j
            b = a + slices + 1
            indices += [a, a + 1, b, a + 1, b + 1, b]
    return positions, normals, indices


def ring_lines(radius, colour, segments=160):
    verts = []
    for k in range(segments):
        for kk in (k, k + 1):
            a = 2 * math.pi * kk / segments
            verts.append(strata.LineVertex((radius * math.cos(a), 0.0, radius * math.sin(a)), colour + (1.0,)))
    return verts


def stars(count=420, seed=7):
    rng = random.Random(seed)
    points = []
    for _ in range(count):
        # Directions on a far sphere, so they stay put as the camera turns.
        z = rng.uniform(-0.35, 1.0)
        t = rng.uniform(0, 2 * math.pi)
        r = math.sqrt(1 - z * z)
        d = 120.0
        b = rng.uniform(0.4, 1.0)
        tint = rng.choice([(1.0, 1.0, 1.0), (0.8, 0.88, 1.0), (1.0, 0.92, 0.8)])
        points.append(strata.Point((d * r * math.cos(t), d * z - 30.0, d * r * math.sin(t)),
                                   rng.choice([1.0, 1.0, 1.5, 2.0]), 0.0, tuple(c * b for c in tint) + (1.0,), 0))
    return points


def build_static(engine, bodies):
    """Everything that does not depend on the camera."""
    positions, normals, indices = uv_sphere(CORE_RADIUS)
    engine.add_mesh(positions, indices, CORE_COLOUR + (1.0,), normals=normals, flags=strata.MESH_UNLIT)

    lighting = engine.lib.lighting_defaults(strata.LIGHTING_SPACE)
    lighting.legacy_ambient = 0.14
    engine.set_lighting(lighting)
    engine.set_point_lights([
        strata.PointLight((0.0, 0.0, 0.0), 22.0, (1.0, 0.88, 0.66), 5.0, 0),
        # A faint cool fill from above so the far side of each body is not black.
        strata.PointLight((0.0, 14.0, 0.0), 30.0, (0.55, 0.65, 1.0), 0.6, 0),
    ])

    items = [strata.RenderItem(strata.PRIMITIVE_SPHERE, p, (2 * r, 2 * r, 2 * r), c + (1.0,)) for p, r, c in bodies]
    lines = []
    for _, radius, colour in RINGS:
        lines += ring_lines(radius, tuple(0.38 * c for c in colour))
    for p, r, c in bodies:
        lines.append(strata.LineVertex((0.0, 0.0, 0.0), (0.55, 0.48, 0.32, 1.0)))
        lines.append(strata.LineVertex(p, tuple(0.45 * x for x in c) + (1.0,)))
    glow = [
        strata.Point((0.0, 0.0, 0.0), 0.0, 7.5, (1.0, 0.72, 0.35, 1.0), strata.POINT_SOFT),
        strata.Point((0.0, 0.0, 0.0), 0.0, 4.2, (1.0, 0.9, 0.6, 1.0), strata.POINT_SOFT),
    ]
    glow += [strata.Point(p, 0.0, 5.0 * r, c + (1.0,), strata.POINT_SOFT) for p, r, c in bodies]
    return items, lines, stars(), glow


def render(engine, frame, size, angle):
    items, lines, star_points, glow = frame
    w, h = size
    eye = (EYE_DISTANCE * math.cos(angle), EYE_HEIGHT, EYE_DISTANCE * math.sin(angle))
    engine.set_camera(engine.lib.look_at(eye, (0.0, -1.0, 0.0), fov_y_degrees=47, aspect=w / h, far_plane=400))
    engine.begin_frame(clear_color=(0.012, 0.016, 0.04, 1.0))
    engine.submit_points(star_points)
    engine.submit_lines(lines)
    engine.submit_primitives(items)
    engine.submit_points(glow)
    engine.end_frame()
    rgba, rw, rh = engine.read_pixels()
    return Image.frombytes("RGBA", (rw, rh), rgba).convert("RGB")


def quantize_loop(frames, colours=64, ramp=8):
    """One GIF palette for every frame: median cut over frames spread round the
    loop, plus a ramp of each ring colour, since median cut spends itself on
    the many orange and teal pixels and loses the few violet bodies."""
    w, h = frames[0].size
    sample = Image.new("RGB", (w, h * 4))
    for k in range(4):
        sample.paste(frames[k * len(frames) // 4], (0, h * k))
    base = sample.quantize(colors=colours - ramp * len(RINGS), method=Image.MEDIANCUT, dither=Image.NONE)
    entries = base.getpalette()[: 3 * (colours - ramp * len(RINGS))]
    for _, _, colour in RINGS:
        for k in range(ramp):
            entries += [round(255 * c * (k + 1) / ramp) for c in colour]
    palette = Image.new("P", (1, 1))
    palette.putpalette(entries + [0] * (768 - len(entries)))
    return [f.quantize(palette=palette, dither=Image.NONE) for f in frames]


def main():
    if "--refresh" in sys.argv:
        refresh()
        return
    lib = strata.load_release(RELEASE)
    bodies = layout()
    still_angle = math.radians(68)
    with strata.Engine(lib, 1920, 1080) as engine:
        frame = build_static(engine, bodies)
        still = render(engine, frame, (1920, 1080), still_angle)
    os.makedirs(os.path.dirname(STILL), exist_ok=True)
    still.resize((960, 540), Image.LANCZOS).save(STILL, quality=86, optimize=True, progressive=True)

    frames = []
    count = 40
    with strata.Engine(lib, 960, 540) as engine:
        frame = build_static(engine, bodies)
        for i in range(count):
            img = render(engine, frame, (960, 540), still_angle + 2 * math.pi * i / count)
            frames.append(img.resize((400, 225), Image.LANCZOS))
    quantized = quantize_loop(frames)
    quantized[0].save(LOOP, save_all=True, append_images=quantized[1:], duration=125, loop=0, optimize=True)
    for path in (STILL, LOOP):
        print(f"wrote {os.path.relpath(path, ROOT)} ({os.path.getsize(path) // 1024} KiB)")


if __name__ == "__main__":
    main()
