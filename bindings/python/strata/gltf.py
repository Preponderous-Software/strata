"""glTF 2.0 loading for the Python binding, through libstrata_gltf.

libstrata_gltf (strata_gltf.dll on Windows) is the glTF module's small C
library (modules/gltf/include/strata/gltf/strata_gltf.h). It is separate from
libstrata and calls it only through strata.h, so this file is separate from
the binding's ABI checks too:

    import strata
    from strata import gltf

    lib = strata.load()
    loader = gltf.load_library()  # beside lib, or STRATA_GLTF_LIBRARY
    with strata.Engine(lib, 640, 360) as engine:
        scene = loader.load(engine, "town.glb", max_meshes=200)
        print(scene.info.mesh_count, scene.report)
        ...
        scene.unload()

Load the libstrata it was built against first (strata.load()), so the
dynamic loader shares that one library between the two.
"""

from __future__ import annotations

import ctypes
import os
import sys
from ctypes import POINTER, Structure, c_char_p, c_float, c_int32, c_int64, c_uint32, c_void_p
from pathlib import Path
from typing import List, Optional

API_VERSION = 1

ALWAYS_MERGE = 0x1
NO_BACK_FACES = 0x2
NO_MIPMAPS = 0x4

COLORS_DISPLAY = 0
COLORS_LINEAR = 1
COLORS_AS_STORED = 2

MASK_BLEND = 0
MASK_OPAQUE = 1

EMISSIVE_UNLIT = 0
EMISSIVE_IGNORE = 1

ALPHA_OPAQUE = 0
ALPHA_MASK = 1
ALPHA_BLEND = 2

_STATUS_NAMES = {1: "invalid argument", 5: "I/O error", 7: "unsupported", 8: "internal error"}


class GltfError(RuntimeError):
    def __init__(self, status: int, message: str):
        super().__init__(message)
        self.status = status


class Options(Structure):
    """StrataGltfOptions: 40 bytes."""

    _fields_ = [
        ("struct_size", c_uint32),
        ("flags", c_uint32),
        ("scene", c_int32),
        ("max_meshes", c_int32),
        ("max_textures", c_int32),
        ("max_texture_size", c_int32),
        ("atlas_size", c_int32),
        ("color_policy", c_int32),
        ("mask_policy", c_int32),
        ("emissive_policy", c_int32),
    ]


class Info(Structure):
    """StrataGltfInfo: 80 bytes."""

    _fields_ = [
        ("primitives_in", c_int32),
        ("primitives_skipped", c_int32),
        ("mesh_count", c_int32),
        ("texture_count", c_int32),
        ("material_count", c_int32),
        ("merged", c_int32),
        ("atlased", c_int32),
        ("atlas_pages", c_int32),
        ("warning_count", c_int32),
        ("reserved", c_int32),
        ("vertices", c_int64),
        ("triangles", c_int64),
        ("bounds_min", c_float * 3),
        ("bounds_max", c_float * 3),
    ]


class MeshInfo(Structure):
    """StrataGltfMeshInfo: 52 bytes."""

    _fields_ = [
        ("mesh_id", c_uint32),
        ("texture_id", c_uint32),
        ("flags", c_uint32),
        ("material", c_int32),
        ("alpha_mode", c_int32),
        ("alpha_cutoff", c_float),
        ("double_sided", c_int32),
        ("primitives", c_int32),
        ("vertex_count", c_int32),
        ("index_count", c_int32),
        ("emissive", c_float * 3),
    ]


def library_file_name() -> str:
    return "strata_gltf.dll" if sys.platform == "win32" else "libstrata_gltf.so"


def resolve_library_path(path: Optional[os.PathLike] = None, beside: Optional[os.PathLike] = None) -> Path:
    """path, else STRATA_GLTF_LIBRARY, else beside the given libstrata, else STRATA_NATIVE_DIR."""
    if path:
        return Path(path)
    explicit = os.environ.get("STRATA_GLTF_LIBRARY")
    if explicit:
        return Path(explicit)
    if beside:
        return Path(beside).resolve().parent / library_file_name()
    directory = os.environ.get("STRATA_NATIVE_DIR")
    if directory:
        return Path(directory) / library_file_name()
    raise GltfError(1, "no libstrata_gltf: pass a path, set STRATA_GLTF_LIBRARY or STRATA_NATIVE_DIR")


class Scene:
    """One loaded glTF scene: its meshes and textures live on the engine until unload()."""

    def __init__(self, loader: "Loader", engine, handle: c_void_p):
        self._loader = loader
        self._engine = engine
        self._handle = handle
        raw = loader.raw
        self.info = Info()
        loader._check(raw.strata_gltf_get_info(handle, ctypes.byref(self.info)), "strata_gltf_get_info")
        self.meshes: List[MeshInfo] = []
        for i in range(self.info.mesh_count):
            mesh = MeshInfo()
            loader._check(raw.strata_gltf_get_mesh(handle, i, ctypes.byref(mesh)), "strata_gltf_get_mesh")
            self.meshes.append(mesh)
        self.report = raw.strata_gltf_report(handle).decode("utf-8", "replace")
        self.report_json = raw.strata_gltf_report_json(handle).decode("utf-8", "replace")

    @property
    def mesh_ids(self) -> List[int]:
        return [m.mesh_id for m in self.meshes]

    def unload(self) -> None:
        """Removes the meshes and textures from the engine (if still open) and frees the scene."""
        if self._handle:
            engine = self._engine._handle if self._engine._handle else None
            self._loader.raw.strata_gltf_unload(engine, self._handle)
            self._handle = c_void_p()

    def __enter__(self) -> "Scene":
        return self

    def __exit__(self, *exc) -> None:
        self.unload()


class Loader:
    """A loaded libstrata_gltf."""

    def __init__(self, cdll: ctypes.CDLL, path: Optional[str] = None):
        self.raw = cdll
        self.path = path
        cdll.strata_gltf_api_version.restype = c_uint32
        cdll.strata_gltf_api_version.argtypes = []
        cdll.strata_gltf_last_error.restype = c_char_p
        cdll.strata_gltf_last_error.argtypes = []
        cdll.strata_gltf_load.restype = c_int32
        cdll.strata_gltf_load.argtypes = [c_void_p, c_char_p, POINTER(Options), POINTER(c_void_p)]
        cdll.strata_gltf_get_info.restype = c_int32
        cdll.strata_gltf_get_info.argtypes = [c_void_p, POINTER(Info)]
        cdll.strata_gltf_get_mesh.restype = c_int32
        cdll.strata_gltf_get_mesh.argtypes = [c_void_p, c_int32, POINTER(MeshInfo)]
        cdll.strata_gltf_report.restype = c_char_p
        cdll.strata_gltf_report.argtypes = [c_void_p]
        cdll.strata_gltf_report_json.restype = c_char_p
        cdll.strata_gltf_report_json.argtypes = [c_void_p]
        cdll.strata_gltf_unload.restype = None
        cdll.strata_gltf_unload.argtypes = [c_void_p, c_void_p]
        self.api_version = int(cdll.strata_gltf_api_version())
        if self.api_version != API_VERSION:
            raise GltfError(2, "libstrata_gltf API %d; this binding speaks %d" % (self.api_version, API_VERSION))

    def last_error(self) -> str:
        return self.raw.strata_gltf_last_error().decode("utf-8", "replace")

    def _check(self, status: int, what: str) -> None:
        if status != 0:
            raise GltfError(status, "%s failed (%s): %s" % (what, _STATUS_NAMES.get(status, status), self.last_error()))

    def load(
        self,
        engine,
        path: os.PathLike,
        scene: int = -1,
        max_meshes: int = 0,
        max_textures: int = 0,
        max_texture_size: int = 0,
        atlas_size: int = 0,
        colors: int = COLORS_DISPLAY,
        mask: int = MASK_BLEND,
        emissive: int = EMISSIVE_UNLIT,
        flags: int = 0,
    ) -> Scene:
        """strata_gltf_load onto a strata.Engine. Zero limits take the engine's maxima."""
        options = Options(
            ctypes.sizeof(Options), flags, scene, max_meshes, max_textures, max_texture_size, atlas_size, colors, mask, emissive
        )
        handle = c_void_p()
        self._check(
            self.raw.strata_gltf_load(engine.handle, os.fsencode(str(path)), ctypes.byref(options), ctypes.byref(handle)),
            "strata_gltf_load",
        )
        return Scene(self, engine, handle)


def load_library(path: Optional[os.PathLike] = None, beside=None) -> Loader:
    """Loads libstrata_gltf. beside may be a strata.Library (its directory is searched)."""
    near = getattr(beside, "path", beside)
    if near is None:
        explicit = os.environ.get("STRATA_LIBRARY")
        near = explicit or None
    resolved = resolve_library_path(path, near)
    if not resolved.is_file():
        raise GltfError(5, "%s does not exist" % resolved)
    return Loader(ctypes.CDLL(str(resolved.resolve())), str(resolved))
