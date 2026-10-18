// Strata USD sync points (RFC 0019 §4-5): compose a layer stack, hydrate it
// into a flat world, and write a world back as a full layer (the base) or as a
// delta (a save). Game-agnostic: every name comes from the world's schema.
//
// Layout written and read:
//   /World                 Xform, defaultPrim of the base layer
//     /Grid                eng:grid:width|height|chunk (int)
//       /C_000_000 ...     one prim per chunk; one int[] attribute per channel
//     /Globals             one int64 attribute per global
//     /Entities/<Coll>/<Prefix><id>   Xform, prepend references = <prefab>; one attribute per field
//   A deleted entity is `over ... (active = false)` in the delta.
//   A variant selection "<prim>|<set>" is a variant-selection opinion.
#pragma once

#include <string>
#include <vector>

#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>

#include "strata/world/world.h"

namespace strata::usd {

// Engine save-layout version (prim paths, chunking, manifest keys).
constexpr int kFormatVersion = 1;

// Compose layers listed WEAKEST FIRST (e.g. content, base, mods..., save)
// into a stage. Every layer is opened by Strata, fresh from disk, and the call
// FAILS if any layer is missing or unreadable. (Left to itself, USD only warns
// about a broken sublayer and composes without it, so a damaged save would load
// as the bare base world.) Returns null and sets `err` on failure.
pxr::UsdStageRefPtr compose(const std::vector<std::string>& layersWeakestFirst, std::string* err = nullptr);

// Read a composed stage into `out`, which must be a schema template
// (strata::world::emptyLike). Any malformed input (bad names, duplicate ids,
// missing fields, wrong types, short chunks) is an error, never a crash.
bool hydrate(const pxr::UsdStageRefPtr& stage, world::World& out, std::string* err = nullptr);

// Save manifest, stored in the save layer's customLayerData under eng:*.
struct Manifest {
    int formatVersion = kFormatVersion;
    std::string gameId;
    int gameSchemaVersion = 1;
    std::string baseHash;           // sha256 hex of the base layer file
    std::vector<std::string> mods;  // "<modId>@<sha256 hex>", load order (weakest first)
    int64_t tick = 0;
};
bool readManifest(const pxr::SdfLayerRefPtr& layer, Manifest& m, std::string* err = nullptr);
void writeManifest(const pxr::SdfLayerRefPtr& layer, const Manifest& m);

// The whole world as `def` prims: the immutable base layer.
pxr::SdfLayerRefPtr writeFull(const world::World& w);
// Only what differs between `baseline` (composed without the save) and
// `current`, plus the manifest. Same inputs -> byte-identical file.
// Throws std::runtime_error on unsupported changes (grid reshaped, an entity's
// prefab changed, an Int field outside the int32 range).
pxr::SdfLayerRefPtr writeDelta(const world::World& baseline, const world::World& current, const Manifest& m);

}  // namespace strata::usd
