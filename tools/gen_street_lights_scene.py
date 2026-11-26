#!/usr/bin/env python3
"""Writes a synthetic night town for clustered light culling (ABI 1.21).

usage: gen_street_lights_scene.py <out.scene> [--lamps N] [--spots N]
           [--culling none|clustered] [--no-shadow] [--size W H] [--header TEXT]

A 5 x 5 grid of blocks with box buildings, a street lamp (a thin post and a
point light) every 3 units along every street on alternating sides, then
trimmed, or topped up with lamps scattered over the blocks, to N lamps; and
--spots down-lights over the crossings. The first lamp casts shadows unless
--no-shadow. A fixed LCG (no random module), so the same arguments always
write the same file byte for byte.
tests/scenes/light_clustered_street_night.scene is its output with
--lamps 240 --spots 16 and the header that file starts with; docs/LIGHTING.md
times the same town with other lamp counts.
"""
import argparse

p = argparse.ArgumentParser()
p.add_argument("out")
p.add_argument("--lamps", type=int, default=240)
p.add_argument("--culling", default="clustered")
p.add_argument("--spots", type=int, default=16)
p.add_argument("--size", type=int, nargs=2, default=[640, 360])
p.set_defaults(shadow=True)
p.add_argument("--no-shadow", dest="shadow", action="store_false")
p.add_argument("--header", default="")
a = p.parse_args()

class Lcg:
    """Numerical Recipes' 32-bit LCG: enough for scenery, stable forever."""

    def __init__(self, seed):
        self.state = seed & 0xFFFFFFFF

    def unit(self):
        self.state = (self.state * 1664525 + 1013904223) & 0xFFFFFFFF
        return self.state / 4294967296.0

    def uniform(self, lo, hi):
        return lo + (hi - lo) * self.unit()

    def randrange(self, n):
        return min(int(self.unit() * n), n - 1)

    def choice(self, seq):
        return seq[self.randrange(len(seq))]

    def shuffle(self, seq):
        for i in range(len(seq) - 1, 0, -1):
            j = self.randrange(i + 1)
            seq[i], seq[j] = seq[j], seq[i]


rng = Lcg(20261125)
L = []
w = L.append
if a.header:
    for line in a.header.splitlines():
        w("# " + line if line else "#")
w("size %d %d" % tuple(a.size))
w("camera 0 34 40  0 0 -4  50 0.5 160")
w("lighting sun 0.0 35  4 2048 80 0.6 0")
w("ambient 0.25 0.3 0.45  0.004 0.005 0.009")
w("clear 0.01 0.012 0.025 1")
if a.culling != "none":
    w("light_culling %s" % a.culling)
# Ground and streets: 5 x 5 blocks, 12 units apart, streets 4 wide.
N, PITCH, STREET = 5, 12.0, 4.0
half = N * PITCH / 2
w("item cube 0 -0.5 0  %g 1 %g  0.22 0.22 0.24" % (N * PITCH + STREET + 8, N * PITCH + STREET + 8))
for i in range(N):
    for j in range(N):
        cx = -half + PITCH * (i + 0.5)
        cz = -half + PITCH * (j + 0.5)
        # Pavement slab, then 2 x 2 buildings of varied height.
        w("item cube %g 0.05 %g  %g 0.1 %g  0.4 0.4 0.42" % (cx, cz, PITCH - STREET, PITCH - STREET))
        for bx in (-1, 1):
            for bz in (-1, 1):
                h = rng.uniform(1.5, 7.0)
                shade = rng.uniform(0.45, 0.75)
                w("item cube %g %g %g  3.2 %g 3.2  %.3f %.3f %.3f" % (
                    cx + bx * 2.0, h / 2 + 0.1, cz + bz * 2.0, h, shade, shade * rng.uniform(0.9, 1.0),
                    shade * rng.uniform(0.85, 1.05)))
# Lamps: along streets (x = const and z = const lines), every 3 units, alternating sides.
lamps = []
lines = [-half + PITCH * k for k in range(N + 1)]
t = -half
pos = []
step = 3.0
while t <= half + 1e-6:
    pos.append(t)
    t += step
for k, s in enumerate(lines):
    for n, t in enumerate(pos):
        side = 1.6 if (n + k) % 2 == 0 else -1.6
        lamps.append((s + side, t))      # street along z
        lamps.append((t, s + side))      # street along x
seen = set()
uniq = []
for x, z in lamps:
    key = (round(x, 2), round(z, 2))
    if key in seen:
        continue
    seen.add(key)
    uniq.append((x, z))
rng.shuffle(uniq)
while len(uniq) < a.lamps:  # extra small lamps over the pavements
    uniq.append((rng.uniform(-half, half), rng.uniform(-half, half)))
uniq = uniq[: a.lamps]
palette = [(1.0, 0.62, 0.28)] * 6 + [(0.85, 0.9, 1.0)] * 2 + [(1.0, 0.3, 0.5), (0.3, 1.0, 0.55), (0.35, 0.55, 1.0)]
for idx, (x, z) in enumerate(uniq):
    c = palette[rng.randrange(len(palette))]
    w("item cylinder %.3f 1.1 %.3f  0.12 2.2 0.12  0.15 0.15 0.16" % (x, z))
    shadow = " shadow" if (a.shadow and idx == 0) else ""
    w("light point %.3f 2.4 %.3f  %.2f  %.2f %.2f %.2f  %.2f%s" % (
        x, z, rng.uniform(3.5, 5.0), c[0], c[1], c[2], rng.uniform(3.5, 5.5), shadow))
# Spot lights: down-lights over the four central crossings and a few shop fronts.
for s in range(a.spots):
    x = rng.choice(lines)
    z = rng.choice(lines)
    if s % 2:
        x += rng.uniform(-4, 4)
    else:
        z += rng.uniform(-4, 4)
    w("light spot %.3f 5 %.3f  8  %.2f -1 %.2f  0.9 0.95 1.0  3  15 30" % (
        x, z, rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3)))
open(a.out, "w").write("\n".join(L) + "\n")
print("%s: %d point lights, %d spot lights" % (a.out, len(uniq), a.spots))
