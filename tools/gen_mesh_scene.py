#!/usr/bin/env python3
"""Writes tests/scenes/_mesh_town.scene, the static-mesh goldens' world.

Deterministic (closed-form, no random module) so re-running it reproduces the
committed file byte for byte. A gently sloped 41 x 41 terrain tile with
2-unit cells carries static meshes the primitives cannot express: rotated
rectangular blocks with gabled roofs, a flat-roofed hexagonal hall, and a
curved road ribbon draped over the slope. Every face is flat-shaded (no
vertex is shared between faces) and wound counter-clockwise seen from
outside, so the engine computes the normals."""
import math
import os

N, CELL, ORIGIN = 41, 2.0, (-40.0, 0.0, -40.0)


def height(x, z):
    return 0.05 * x - 0.03 * z + 0.6 * math.sin(z * 0.08) * math.cos(x * 0.05)


def ground(x, z):
    """Bilinear height of the tile at world x, z."""
    fx, fz = (x - ORIGIN[0]) / CELL, (z - ORIGIN[2]) / CELL
    i, j = min(max(int(math.floor(fx)), 0), N - 2), min(max(int(math.floor(fz)), 0), N - 2)
    tx, tz = fx - i, fz - j
    h = [[height(ORIGIN[0] + (i + a) * CELL, ORIGIN[2] + (j + b) * CELL) for a in (0, 1)] for b in (0, 1)]
    return (h[0][0] * (1 - tx) + h[0][1] * tx) * (1 - tz) + (h[1][0] * (1 - tx) + h[1][1] * tx) * tz


def fmt(v):
    s = f"{v:.3f}".rstrip("0").rstrip(".")
    return "0" if s in ("", "-0") else s


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


class Mesh:
    def __init__(self, colour):
        self.colour, self.verts, self.cols, self.tris = colour, [], [], []

    def tri(self, a, b, c, outward, colour):
        """Adds a flat triangle, wound so its normal points along `outward`."""
        if dot(cross(sub(b, a), sub(c, a)), outward) < 0:
            b, c = c, b
        base = len(self.verts)
        self.verts += [a, b, c]
        self.cols += [colour] * 3
        self.tris.append((base, base + 1, base + 2))

    def poly(self, points, outward, colour):
        for k in range(1, len(points) - 1):
            self.tri(points[0], points[k], points[k + 1], outward, colour)

    def lines(self):
        out = [f"mesh {' '.join(fmt(c) for c in self.colour)}"]
        for k in range(0, len(self.verts), 12):
            out.append("vertices " + "  ".join(" ".join(fmt(c) for c in v) for v in self.verts[k:k + 12]))
        for k in range(0, len(self.cols), 12):
            out.append("vcolors " + "  ".join(" ".join(fmt(c) for c in v) for v in self.cols[k:k + 12]))
        for k in range(0, len(self.tris), 16):
            out.append("triangles " + "  ".join(" ".join(str(i) for i in t) for t in self.tris[k:k + 16]))
        return out


def gabled(mesh, cx, cz, length, width, angle, eave, rise, wall, roof):
    """A rotated rectangular block with its ridge along its length."""
    ca, sa = math.cos(angle), math.sin(angle)

    def at(u, v):
        return (cx + u * ca - v * sa, cz + u * sa + v * ca)

    corners = [at(u, v) for u, v in ((-length / 2, -width / 2), (length / 2, -width / 2),
                                     (length / 2, width / 2), (-length / 2, width / 2))]
    base = min(ground(x, z) for x, z in corners) - 0.4
    top = max(ground(x, z) for x, z in corners) + eave
    centre = (cx, (base + top) / 2, cz)
    for k in range(4):
        (x0, z0), (x1, z1) = corners[k], corners[(k + 1) % 4]
        quad = [(x0, base, z0), (x1, base, z1), (x1, top, z1), (x0, top, z0)]
        mid = ((x0 + x1) / 2, centre[1], (z0 + z1) / 2)
        mesh.poly(quad, sub(mid, centre), wall)
        if k % 2 == 1:  # a short side: the gable triangle above it
            rx, rz = at((1 if k == 1 else -1) * length / 2, 0)
            mesh.tri((x0, top, z0), (x1, top, z1), (rx, top + rise, rz), sub(mid, centre), wall)
    r0, r1 = at(-length / 2, 0), at(length / 2, 0)
    for side in (-1, 1):
        e0, e1 = at(-length / 2, side * width / 2), at(length / 2, side * width / 2)
        plane = [(e0[0], top, e0[1]), (e1[0], top, e1[1]), (r1[0], top + rise, r1[1]), (r0[0], top + rise, r0[1])]
        outward = (-side * sa, 1.0, side * ca)
        mesh.poly(plane, outward, roof)


def hall(mesh, cx, cz, radius, tall, wall, roof):
    """A flat-roofed hexagonal prism."""
    ring = [(cx + radius * math.cos(k * math.pi / 3 + 0.2), cz + radius * math.sin(k * math.pi / 3 + 0.2))
            for k in range(6)]
    base = min(ground(x, z) for x, z in ring) - 0.4
    top = max(ground(x, z) for x, z in ring) + tall
    for k in range(6):
        (x0, z0), (x1, z1) = ring[k], ring[(k + 1) % 6]
        mesh.poly([(x0, base, z0), (x1, base, z1), (x1, top, z1), (x0, top, z0)],
                  ((x0 + x1) / 2 - cx, 0.0, (z0 + z1) / 2 - cz), wall)
    mesh.poly([(x, top, z) for x, z in ring], (0.0, 1.0, 0.0), roof)


def road(mesh, width, colour):
    """A curved ribbon draped 6 cm above the ground."""
    path = [(-40 + t, 8 * math.sin(t * 0.06) - 4) for t in range(0, 81)]
    left, right = [], []
    for k, (x, z) in enumerate(path):
        (xa, za), (xb, zb) = path[max(k - 1, 0)], path[min(k + 1, len(path) - 1)]
        dx, dz = xb - xa, zb - za
        n = math.hypot(dx, dz)
        ox, oz = -dz / n * width / 2, dx / n * width / 2
        left.append((x + ox, ground(x + ox, z + oz) + 0.06, z + oz))
        right.append((x - ox, ground(x - ox, z - oz) + 0.06, z - oz))
    for k in range(len(path) - 1):
        mesh.poly([left[k], left[k + 1], right[k + 1], right[k]], (0.0, 1.0, 0.0), colour)


lines = ["# Generated by tools/gen_mesh_scene.py -- do not edit by hand.",
         f"terrain {fmt(ORIGIN[0])} {fmt(ORIGIN[1])} {fmt(ORIGIN[2])}  {fmt(CELL)} {N} {N}  0.24 0.42 0.16"]
for j in range(N):
    lines.append("heights " + " ".join(fmt(height(ORIGIN[0] + i * CELL, ORIGIN[2] + j * CELL)) for i in range(N)))

houses = Mesh((0.8, 0.75, 0.65))
blocks = [(-22, -18, 12, 8, 0.35), (-6, -20, 10, 7, -0.2), (12, -17, 14, 9, 0.1), (26, -14, 9, 7, 0.55),
          (-24, 12, 11, 8, -0.4), (-7, 15, 13, 8, 0.25), (24, 14, 10, 8, -0.15)]
for k, (cx, cz, ln, wd, ang) in enumerate(blocks):
    wall = (0.78 - 0.05 * (k % 3), 0.70 - 0.03 * (k % 2), 0.58 + 0.04 * (k % 3))
    roof = (0.35 + 0.08 * (k % 2), 0.22, 0.18) if k % 3 else (0.28, 0.30, 0.34)
    gabled(houses, cx, cz, ln, wd, ang, 3.2 + 0.6 * (k % 2), 0.5 * wd * math.tan(math.radians(32)), wall, roof)
hall(houses, 8, 16, 6.5, 6.0, (0.62, 0.64, 0.68), (0.30, 0.30, 0.32))

street = Mesh((0.18, 0.18, 0.19))
road(street, 6.0, (0.18, 0.18, 0.19))

lines += houses.lines() + street.lines()
out = os.path.join(os.path.dirname(__file__), "..", "tests", "scenes", "_mesh_town.scene")
with open(out, "w", newline="\n") as f:
    f.write("\n".join(lines) + "\n")
print(f"wrote {os.path.normpath(out)} ({len(lines)} lines)")
