// A toy game used only by the save-core tests, so the engine is tested without
// any real game's code (RFC 0019 G5). Schema v1 and v2 exist so migrations can
// be tested: v2 adds an int64 field and a global.
#pragma once

#include <cstdint>
#include <string>

#include "strata/save/slot.h"
#include "strata/world/world.h"

namespace toy {

constexpr const char* kGameId = "strata-toy";

strata::world::World schema(int version);  // empty world for a schema version
strata::world::World generate(uint64_t seed, int w, int h, int agents, int version = 2);
void step(strata::world::World& w);        // one deterministic tick (any version)
strata::save::Schema saveSchema(int version);  // with the v1 -> v2 migration
strata::save::Layers layers(bool fast = false, bool heavy = false);
std::string dataDir();

}  // namespace toy
