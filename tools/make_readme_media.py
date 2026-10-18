#!/usr/bin/env python3
"""Renders docs/media/day-cycle.gif, the README's day-cycle animation.

The L2 village (tests/scenes/_village_items.scene) with four shadow cascades
and the analytic sky, drawn by strata-golden at time of day 0.22 to 0.98, plus
the L3 campfire and lamps, which only show once the sun is down. Needs a built
strata-golden and Pillow (pip install pillow); run headless with
LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe.

    tools/make_readme_media.py build/strata-golden
"""
import os
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SCENES = os.path.join(ROOT, "tests", "scenes")
OUT = os.path.join(ROOT, "docs", "media", "day-cycle.gif")

SCENE = """size 640 360
camera -40 20 40  0 0 -4  60 0.3 400
lighting sun {t:.3f} 35  4 2048 150 0.6 1
clear 0 0 0 1 sky
include _village_items.scene
item cylinder -2 0.75 4.5  0.5 0.25 0.5  0.35 0.22 0.12
light point -2 1.6 4.5  16  1.0 0.55 0.2  {fire:.2f} shadow
light point -11 2.8 -4  10  1.0 0.8 0.5  {lamp:.2f}
light point 7.5 2.8 -4.5  10  1.0 0.8 0.5  {lamp:.2f}
"""


def night_amount(t):
    """0 by day, 1 at night, ramping across dusk and dawn."""
    def ramp(a, b, x):
        return min(max((x - a) / (b - a), 0.0), 1.0)
    return max(ramp(0.72, 0.80, t), 1.0 - ramp(0.20, 0.28, t))


def main():
    golden = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "strata-golden")
    times = [0.22 + 0.02 * i for i in range(39)]
    frames = []
    with tempfile.TemporaryDirectory() as tmp:
        for i, t in enumerate(times):
            n = night_amount(t)
            # Beside the village so its include resolves; the leading '_'
            # keeps strata-golden check from treating it as a test.
            scene = os.path.join(SCENES, "_readme_frame.scene")
            with open(scene, "w") as f:
                f.write(SCENE.format(t=t, fire=6 * n, lamp=4 * n))
            png = os.path.join(tmp, f"frame{i:02d}.png")
            try:
                subprocess.run([golden, "render", scene, png], check=True, stdout=subprocess.DEVNULL)
            finally:
                os.remove(scene)
            frames.append(Image.open(png).convert("RGB").resize((480, 270), Image.LANCZOS))
    palette = [f.quantize(colors=128, method=Image.MEDIANCUT, dither=Image.NONE) for f in frames]
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    palette[0].save(OUT, save_all=True, append_images=palette[1:], duration=120, loop=0, optimize=True)
    print(f"wrote {os.path.relpath(OUT, ROOT)} ({len(frames)} frames, {os.path.getsize(OUT) // 1024} KiB)")


if __name__ == "__main__":
    main()
