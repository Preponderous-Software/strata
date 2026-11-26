#!/usr/bin/env python3
"""Writes the synthetic glTF test files in modules/gltf/tests/assets/.

Standard library only; deterministic (the same bytes every run), so the
committed files can be checked with `make_fixtures.py --check`. Every file is
made from scratch here: boxes, quads and procedurally drawn images. Repeating,
mipmapped ground textures are kept low in contrast: mip selection on slanted
surfaces differs a little between Mesa versions, and a low-contrast checker
keeps that under the golden tolerance.

    python3 modules/gltf/tests/make_fixtures.py          # rewrite the assets
    python3 modules/gltf/tests/make_fixtures.py --check  # exit 1 if they differ
"""

import base64
import json
import math
import struct
import sys
import zlib
from pathlib import Path

ASSETS = Path(__file__).resolve().parent / "assets"

FLOAT, UINT8, UINT16, UINT32 = 5126, 5121, 5123, 5125
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963
NEAREST, LINEAR, LINEAR_MIPMAP_LINEAR = 9728, 9729, 9987
CLAMP, REPEAT = 33071, 10497


# ------------------------------------------------------------------ images

def png(width: int, height: int, pixel) -> bytes:
    """An RGBA8 PNG; pixel(x, y) -> (r, g, b, a) bytes."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(pixel(x, y))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", stored_zlib(bytes(raw))) + chunk(b"IEND", b"")


def stored_zlib(data: bytes) -> bytes:
    """A zlib stream of uncompressed (stored) deflate blocks: the same bytes
    with every zlib version, unlike zlib.compress."""
    out = bytearray(b"\x78\x01")
    for start in range(0, max(len(data), 1), 65535):
        block = data[start:start + 65535]
        final = 1 if start + 65535 >= len(data) else 0
        out += struct.pack("<BHH", final, len(block), len(block) ^ 0xFFFF) + block
    return bytes(out) + struct.pack(">I", zlib.adler32(data) & 0xFFFFFFFF)


def checker(size: int, cells: int, a, b):
    step = size // cells
    return png(size, size, lambda x, y: a if ((x // step) + (y // step)) % 2 == 0 else b)


def quadrants(size: int):
    """Four coloured quadrants and a white corner marker: shows uv orientation."""
    def pixel(x, y):
        if x < size // 8 and y < size // 8:
            return (255, 255, 255, 255)
        left, top = x < size // 2, y < size // 2
        if top and left:
            return (220, 40, 40, 255)
        if top:
            return (40, 180, 60, 255)
        if left:
            return (40, 80, 220, 255)
        return (240, 200, 40, 255)
    return png(size, size, pixel)


def disc(size: int):
    """An orange disc with soft alpha on transparent black: for MASK and BLEND."""
    def pixel(x, y):
        r = math.hypot(x + 0.5 - size / 2, y + 0.5 - size / 2) / (size / 2)
        alpha = max(0.0, min(1.0, (1.0 - r) * 4.0))
        return (250, 140, 30, int(round(alpha * 255)))
    return png(size, size, pixel)


# ------------------------------------------------------------------ geometry

def box(sx=1.0, sy=1.0, sz=1.0, centre=(0.0, 0.0, 0.0)):
    """24 vertices, outward normals, a 0..1 uv square per face, CCW from outside."""
    faces = [  # normal, u axis, v axis (v runs down the image)
        ((1, 0, 0), (0, 0, -1), (0, -1, 0)),
        ((-1, 0, 0), (0, 0, 1), (0, -1, 0)),
        ((0, 1, 0), (1, 0, 0), (0, 0, 1)),
        ((0, -1, 0), (1, 0, 0), (0, 0, -1)),
        ((0, 0, 1), (1, 0, 0), (0, -1, 0)),
        ((0, 0, -1), (-1, 0, 0), (0, -1, 0)),
    ]
    half = (sx / 2, sy / 2, sz / 2)
    pos, nrm, uv, idx = [], [], [], []
    for n, u, v in faces:
        base = len(pos) // 3
        for cu, cv in ((0, 0), (1, 0), (1, 1), (0, 1)):
            for k in range(3):
                pos.append(centre[k] + half[k] * (n[k] + (2 * cu - 1) * u[k] + (2 * cv - 1) * v[k]))
            nrm.extend(n)
            uv.extend((cu, cv))
        # (0,0) (1,0) (1,1) (0,1) with u right and v down seen from outside is clockwise: wind 0,2,1 / 0,3,2.
        idx.extend((base, base + 2, base + 1, base, base + 3, base + 2))
    return pos, nrm, uv, idx


def quad_xz(size: float, y: float = 0.0, uv_repeat: float = 1.0):
    """A horizontal square facing +Y."""
    h = size / 2
    pos = [-h, y, -h, h, y, -h, h, y, h, -h, y, h]
    nrm = [0, 1, 0] * 4
    uv = [0, 0, uv_repeat, 0, uv_repeat, uv_repeat, 0, uv_repeat]
    return pos, nrm, uv, [0, 2, 1, 0, 3, 2]


def quad_xy(width: float, height: float, centre=(0.0, 0.0, 0.0)):
    """An upright rectangle facing +Z, uv (0,0) at its top-left."""
    cx, cy, cz = centre
    w, h = width / 2, height / 2
    pos = [cx - w, cy + h, cz, cx + w, cy + h, cz, cx + w, cy - h, cz, cx - w, cy - h, cz]
    nrm = [0, 0, 1] * 4
    uv = [0, 0, 1, 0, 1, 1, 0, 1]
    return pos, nrm, uv, [0, 3, 2, 0, 2, 1]


# ------------------------------------------------------------------ builder

class Gltf:
    def __init__(self, generator: str):
        self.j = {"asset": {"version": "2.0", "generator": generator}}
        self.bin = bytearray()

    def _list(self, key):
        return self.j.setdefault(key, [])

    def _add(self, key, value) -> int:
        items = self._list(key)
        items.append(value)
        return len(items) - 1

    def view(self, data: bytes, target=None) -> int:
        while len(self.bin) % 4:
            self.bin.append(0)
        v = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            v["target"] = target
        self.bin.extend(data)
        return self._add("bufferViews", v)

    def accessor(self, values, component, kind, normalized=False, target=ARRAY_BUFFER, minmax=False) -> int:
        fmt = {FLOAT: "f", UINT8: "B", UINT16: "H", UINT32: "I"}[component]
        comps = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[kind]
        data = struct.pack("<%d%s" % (len(values), fmt), *values)
        if component in (UINT8, UINT16) and len(data) % 4:
            data += b"\0" * (4 - len(data) % 4)
        a = {"bufferView": self.view(data, target), "componentType": component, "count": len(values) // comps, "type": kind}
        if normalized:
            a["normalized"] = True
        if minmax:
            a["min"] = [min(values[i::comps]) for i in range(comps)]
            a["max"] = [max(values[i::comps]) for i in range(comps)]
        return self._add("accessors", a)

    def primitive(self, pos, nrm=None, uv=None, idx=None, material=None, colors=None, mode=None, index_type=UINT16):
        attrs = {"POSITION": self.accessor(pos, FLOAT, "VEC3", minmax=True)}
        if nrm is not None:
            attrs["NORMAL"] = self.accessor(nrm, FLOAT, "VEC3")
        if uv is not None:
            attrs["TEXCOORD_0"] = self.accessor(uv, FLOAT, "VEC2")
        if colors is not None:
            attrs["COLOR_0"] = colors
        p = {"attributes": attrs}
        if idx is not None:
            p["indices"] = self.accessor(idx, index_type, "SCALAR", target=ELEMENT_ARRAY_BUFFER)
        if material is not None:
            p["material"] = material
        if mode is not None:
            p["mode"] = mode
        return p

    def mesh(self, name, primitives) -> int:
        return self._add("meshes", {"name": name, "primitives": primitives})

    def node(self, **fields) -> int:
        return self._add("nodes", fields)

    def material(self, **fields) -> int:
        return self._add("materials", fields)

    def sampler(self, mag, minf, wrap_s, wrap_t) -> int:
        return self._add("samplers", {"magFilter": mag, "minFilter": minf, "wrapS": wrap_s, "wrapT": wrap_t})

    def image_view(self, data: bytes, name: str) -> int:
        return self._add("images", {"name": name, "bufferView": self.view(data), "mimeType": "image/png"})

    def image_uri(self, uri: str, name: str) -> int:
        return self._add("images", {"name": name, "uri": uri})

    def texture(self, image, sampler=None) -> int:
        t = {"source": image}
        if sampler is not None:
            t["sampler"] = sampler
        return self._add("textures", t)

    def scene(self, nodes):
        self.j["scenes"] = [{"nodes": nodes}]
        self.j["scene"] = 0

    def gltf_bytes(self, bin_uri=None) -> bytes:
        j = dict(self.j)
        uri = bin_uri or "data:application/octet-stream;base64," + base64.b64encode(bytes(self.bin)).decode("ascii")
        j["buffers"] = [{"byteLength": len(self.bin), "uri": uri}]
        return (json.dumps(j, indent=1, sort_keys=True) + "\n").encode("utf-8")

    def glb_bytes(self) -> bytes:
        j = dict(self.j)
        j["buffers"] = [{"byteLength": len(self.bin)}]
        text = json.dumps(j, separators=(",", ":"), sort_keys=True).encode("utf-8")
        text += b" " * ((4 - len(text) % 4) % 4)
        binary = bytes(self.bin) + b"\0" * ((4 - len(self.bin) % 4) % 4)
        body = struct.pack("<II", len(text), 0x4E4F534A) + text + struct.pack("<II", len(binary), 0x004E4942) + binary
        return struct.pack("<III", 0x46546C67, 2, 12 + len(body)) + body


# ------------------------------------------------------------------ fixtures

def cube():
    """One box, one material with a base colour factor; embedded data: URI buffer."""
    g = Gltf("strata gltf fixtures: cube")
    red = g.material(name="red", pbrMetallicRoughness={"baseColorFactor": [0.8, 0.12, 0.1, 1.0], "metallicFactor": 0})
    pos, nrm, uv, idx = box(1, 1, 1)
    m = g.mesh("cube", [g.primitive(pos, nrm, None, idx, red)])
    g.scene([g.node(name="cube", mesh=m)])
    return {"cube.gltf": g.gltf_bytes()}


def textured_quad():
    """A .glb: an upright quad with an embedded PNG (nearest, clamped) over a checker ground (repeating)."""
    g = Gltf("strata gltf fixtures: textured_quad")
    s_nearest = g.sampler(NEAREST, NEAREST, CLAMP, CLAMP)
    s_repeat = g.sampler(LINEAR, LINEAR_MIPMAP_LINEAR, REPEAT, REPEAT)
    t_quad = g.texture(g.image_view(quadrants(16), "quadrants"), s_nearest)
    t_ground = g.texture(g.image_view(checker(64, 2, (190, 190, 185, 255), (130, 130, 125, 255)), "checker"), s_repeat)
    m_quad = g.material(name="quadrants", pbrMetallicRoughness={"baseColorTexture": {"index": t_quad}, "metallicFactor": 0})
    m_ground = g.material(name="ground", pbrMetallicRoughness={"baseColorTexture": {"index": t_ground}, "metallicFactor": 0})
    pos, nrm, uv, idx = quad_xy(2, 2, (0, 1.05, 0))
    quad = g.mesh("quad", [g.primitive(pos, nrm, uv, idx, m_quad)])
    pos, nrm, uv, idx = quad_xz(6, 0.0, uv_repeat=4)
    ground = g.mesh("ground", [g.primitive(pos, nrm, uv, idx, m_ground)])
    g.scene([g.node(name="quad", mesh=quad), g.node(name="ground", mesh=ground)])
    return {"textured_quad.glb": g.glb_bytes()}


def hierarchy():
    """External .bin and .png; nested TRS and matrix transforms, a reused mesh, a
    mirrored node (negative scale) and a mesh with two primitives."""
    g = Gltf("strata gltf fixtures: hierarchy")
    s = g.sampler(LINEAR, LINEAR_MIPMAP_LINEAR, REPEAT, REPEAT)
    t = g.texture(g.image_uri("hierarchy%20grid.png", "grid"), s)
    m_grid = g.material(name="grid", pbrMetallicRoughness={"baseColorTexture": {"index": t}, "metallicFactor": 0})
    m_blue = g.material(name="blue", pbrMetallicRoughness={"baseColorFactor": [0.15, 0.3, 0.85, 1], "metallicFactor": 0})
    m_gold = g.material(name="gold", pbrMetallicRoughness={"baseColorFactor": [0.9, 0.7, 0.2, 1], "metallicFactor": 0})
    pos, nrm, uv, idx = box(1, 1, 1)
    block = g.mesh("block", [g.primitive(pos, nrm, uv, idx, m_grid)])
    p1 = box(1, 0.25, 1, (0, 0.125, 0))
    p2 = box(0.4, 0.6, 0.4, (0, 0.55, 0))
    lamp = g.mesh("post", [g.primitive(p1[0], p1[1], None, p1[3], m_blue), g.primitive(p2[0], p2[1], None, p2[3], m_gold)])
    pos, nrm, uv, idx = quad_xz(8, 0.0, uv_repeat=8)
    ground = g.mesh("ground", [g.primitive(pos, nrm, uv, idx, m_grid)])
    # An L-shaped wedge: asymmetric, so a mirrored copy is visibly mirrored.
    wedge_pos = [0, 0, 0, 1.2, 0, 0, 0, 0, -0.6, 1.2, 0, -0.6, 0, 1, 0, 0, 1, -0.6]
    wedge_idx = [0, 2, 1, 1, 2, 3,  # bottom (faces -y)
                 0, 1, 4,  # front
                 2, 5, 3,  # back
                 0, 4, 5, 0, 5, 2,  # left
                 1, 3, 5, 1, 5, 4]  # slope
    wedge = g.mesh("wedge", [g.primitive(wedge_pos, None, None, wedge_idx, m_gold)])
    c = math.cos(math.radians(30))
    s30 = math.sin(math.radians(30))
    child_a = g.node(name="block on top", mesh=block, translation=[0, 1.0, 0], scale=[0.6, 0.6, 0.6],
                     rotation=[0, math.sin(math.radians(22.5)), 0, math.cos(math.radians(22.5))])
    child_b = g.node(name="post (matrix)", mesh=lamp,
                     matrix=[c, 0, -s30, 0, 0, 1, 0, 0, s30, 0, c, 0, 1.6, -0.5, 0.4, 1])
    root = g.node(name="root", mesh=block, translation=[-0.8, 0.5, 0], children=[child_a, child_b])
    wedge_r = g.node(name="wedge", mesh=wedge, translation=[0.6, 0, 1.6])
    wedge_l = g.node(name="wedge mirrored", mesh=wedge, translation=[-0.6, 0, 1.6], scale=[-1, 1, 1])
    floor = g.node(name="ground", mesh=ground)
    g.scene([root, wedge_r, wedge_l, floor])
    grid = checker(32, 4, (225, 225, 215, 255), (110, 150, 115, 255))
    return {"hierarchy.gltf": g.gltf_bytes("hierarchy.bin"), "hierarchy.bin": bytes(g.bin), "hierarchy grid.png": grid}


def vertex_colors():
    """A .glb with COLOR_0 in three encodings and no normals (flat shading):
    float RGB, normalized uint8 RGBA, normalized uint16 RGBA; a triangle strip,
    a triangle fan and a non-indexed list."""
    g = Gltf("strata gltf fixtures: vertex_colors")
    white = g.material(name="white", pbrMetallicRoughness={"metallicFactor": 0})
    # A pyramid, non-indexed, float RGB colours per corner.
    apex, corners = (0, 1.2, 0), [(-0.8, 0, 0.8), (0.8, 0, 0.8), (0.8, 0, -0.8), (-0.8, 0, -0.8)]
    pos, col = [], []
    palette = [(1, 0.1, 0.1), (0.1, 1, 0.1), (0.1, 0.2, 1), (1, 1, 0.1)]
    for i in range(4):
        a, b = corners[i], corners[(i + 1) % 4]
        for p, c in ((a, palette[i]), (b, palette[(i + 1) % 4]), (apex, (1, 1, 1))):
            pos.extend(p)
            col.extend(c)
    pyramid = g.primitive([v - 1.1 if k % 3 == 0 else v for k, v in enumerate(pos)], material=white,
                          colors=g.accessor(col, FLOAT, "VEC3"))
    # A strip ribbon, normalized uint8 RGBA.
    strip_pos, strip_col = [], []
    for i in range(8):
        x = 0.4 + i * 0.25
        for top in (True, False):
            strip_pos.extend((x, 0.9 if top else 0.1, 0.3 * math.sin(i)))
            t = i / 7
            strip_col.extend((int(255 * (1 - t)), int(255 * t), 200 if top else 40, 255))
    strip = g.primitive(strip_pos, idx=list(range(16)), material=white, mode=5,
                        colors=g.accessor(strip_col, UINT8, "VEC4", normalized=True))
    # A fan disc on the ground, normalized uint16 RGBA (a hue wheel).
    fan_pos, fan_col = [0.0, 0.01, 1.6], [65535, 65535, 65535, 65535]
    for i in range(17):
        a = 2 * math.pi * i / 16
        fan_pos.extend((0.9 * math.cos(a), 0.01, 1.6 - 0.9 * math.sin(a)))
        rgb = [0.5 + 0.5 * math.cos(a - k * 2 * math.pi / 3) for k in range(3)]
        fan_col.extend([int(65535 * v) for v in rgb] + [65535])
    fan = g.primitive(fan_pos, idx=list(range(18)), material=white, mode=6, index_type=UINT32,
                      colors=g.accessor(fan_col, UINT16, "VEC4", normalized=True))
    m = g.mesh("coloured", [pyramid, strip, fan])
    pos, nrm, uv, idx = quad_xz(5, 0.0)
    grey = g.material(name="grey", pbrMetallicRoughness={"baseColorFactor": [0.55, 0.55, 0.5, 1], "metallicFactor": 0})
    ground = g.mesh("ground", [g.primitive(pos, nrm, None, idx, grey)])
    g.scene([g.node(name="coloured", mesh=m), g.node(name="ground", mesh=ground)])
    return {"vertex_colors.glb": g.glb_bytes()}


def alpha_modes():
    """OPAQUE, MASK and BLEND versions of one soft-edged disc texture, a
    doubleSided leaf seen from behind, an unlit panel with KHR_texture_transform
    and an emissive lamp (emissiveFactor with KHR_materials_emissive_strength)."""
    g = Gltf("strata gltf fixtures: alpha_modes")
    g.j["extensionsUsed"] = ["KHR_materials_emissive_strength", "KHR_materials_unlit", "KHR_texture_transform"]
    s = g.sampler(LINEAR, LINEAR_MIPMAP_LINEAR, CLAMP, CLAMP)
    t_disc = g.texture(g.image_view(disc(64), "disc"), s)
    t_quad = g.texture(g.image_view(quadrants(16), "quadrants"), g.sampler(NEAREST, NEAREST, REPEAT, REPEAT))
    base = {"baseColorTexture": {"index": t_disc}, "metallicFactor": 0}
    opaque = g.material(name="disc opaque", pbrMetallicRoughness=base)
    mask = g.material(name="disc mask", pbrMetallicRoughness=base, alphaMode="MASK", alphaCutoff=0.5)
    blend = g.material(name="disc blend", pbrMetallicRoughness=dict(base, baseColorFactor=[1, 1, 1, 0.6]), alphaMode="BLEND")
    leaf = g.material(name="leaf", doubleSided=True,
                      pbrMetallicRoughness={"baseColorFactor": [0.2, 0.7, 0.25, 1], "metallicFactor": 0})
    unlit = g.material(name="unlit panel", extensions={"KHR_materials_unlit": {}},
                       pbrMetallicRoughness={"baseColorTexture": {"index": t_quad, "extensions": {
                           "KHR_texture_transform": {"offset": [0.5, 0], "scale": [2, 2], "rotation": 0}}}})
    grey = g.material(name="grey", pbrMetallicRoughness={"baseColorFactor": [0.5, 0.5, 0.55, 1], "metallicFactor": 0})
    lamp = g.material(name="lamp", emissiveFactor=[1.0, 0.6, 0.2],
                      extensions={"KHR_materials_emissive_strength": {"emissiveStrength": 0.5}},
                      pbrMetallicRoughness={"baseColorFactor": [0.2, 0.2, 0.2, 1], "metallicFactor": 0})
    nodes = []
    for i, mat in enumerate((opaque, mask, blend)):
        pos, nrm, uv, idx = quad_xy(1.2, 1.2, (-1.5 + 1.5 * i, 0.8, 0))
        nodes.append(g.node(name="disc %d" % i, mesh=g.mesh("disc %d" % i, [g.primitive(pos, nrm, uv, idx, mat)])))
    # The leaf faces away from the camera (-Z); only its back face is seen.
    pos, nrm, uv, idx = quad_xy(1.0, 0.6, (0, 0.3, 0))
    pos = [v if k % 3 != 2 else 0.8 for k, v in enumerate(pos)]
    nrm = [0, 0, -1] * 4
    idx = [0, 2, 3, 0, 1, 2]
    nodes.append(g.node(name="leaf", mesh=g.mesh("leaf", [g.primitive(pos, nrm, None, idx, leaf)]), translation=[-1.2, 0, 0.2]))
    pos, nrm, uv, idx = quad_xy(0.8, 0.8, (1.4, 0.45, 0.9))
    nodes.append(g.node(name="panel", mesh=g.mesh("panel", [g.primitive(pos, nrm, uv, idx, unlit)])))
    pos, nrm, uv, idx = box(0.3, 0.3, 0.3, (2.2, 0.15, 0.2))
    nodes.append(g.node(name="lamp", mesh=g.mesh("lamp", [g.primitive(pos, nrm, None, idx, lamp)])))
    pos, nrm, uv, idx = quad_xz(6, 0.0)
    nodes.append(g.node(name="ground", mesh=g.mesh("ground", [g.primitive(pos, nrm, None, idx, grey)])))
    g.scene(nodes)
    return {"alpha_modes.gltf": g.gltf_bytes()}


def all_files():
    files = {}
    for make in (cube, textured_quad, hierarchy, vertex_colors, alpha_modes):
        files.update(make())
    return files


def main(argv):
    check = "--check" in argv
    files = all_files()
    ASSETS.mkdir(parents=True, exist_ok=True)
    stale = []
    for name, data in sorted(files.items()):
        path = ASSETS / name
        if check:
            if not path.is_file() or path.read_bytes() != data:
                stale.append(name)
        else:
            path.write_bytes(data)
            print("wrote %s (%d bytes)" % (path, len(data)))
    if stale:
        print("stale fixtures (run make_fixtures.py): " + ", ".join(stale))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
