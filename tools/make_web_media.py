#!/usr/bin/env python3
"""Renders docs/media/browser-day-cycle.gif and docs/media/browser-demo.png.

The README's browser images: the village-day demo built with Emscripten,
running in headless Chromium (WebGL 2 on SwiftShader), captured by
tools/capture_web_demo.cjs while its clock is scrubbed through a day. Needs
Node with Playwright (see that script) and Pillow (pip install pillow).

    tools/make_web_media.py build-web
"""
import os
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
MEDIA = os.path.join(ROOT, "docs", "media")


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build-web")
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["node", os.path.join(ROOT, "tools", "capture_web_demo.cjs"), build, tmp], check=True)
        names = sorted(n for n in os.listdir(tmp) if n.startswith("frame"))
        # The page's heading and the canvas, without the empty margin below.
        frames = [Image.open(os.path.join(tmp, n)).convert("RGB").crop((16, 12, 1008, 660))
                  .resize((620, 405), Image.LANCZOS) for n in names]
        Image.open(os.path.join(tmp, "page.png")).convert("RGB").crop((0, 0, 1024, 672)).save(
            os.path.join(MEDIA, "browser-demo.png"), optimize=True)
    palette = [f.quantize(colors=160, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE) for f in frames]
    out = os.path.join(MEDIA, "browser-day-cycle.gif")
    palette[0].save(out, save_all=True, append_images=palette[1:], duration=140, loop=0, optimize=True)
    print(f"wrote {os.path.relpath(out, ROOT)} ({len(frames)} frames, {os.path.getsize(out) // 1024} KiB)")


if __name__ == "__main__":
    main()
