// Sends a Plan to an engine through Strata's public C ABI (strata.h 1.15 or
// later: per-vertex RGBA colours), and takes it away again.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "strata/gltf/plan.hpp"
#include "strata/strata.h"

namespace strata::gltf {

struct Uploaded {
    std::vector<uint32_t> textureIds;  // one per Plan::textures entry, in order
    std::vector<uint32_t> meshIds;     // one per Plan::meshes entry, in order
};

// Creates every texture, then every mesh. On the first failure, everything
// already created is destroyed again and false is returned with the failing
// call's status and strata_last_error() text in *status / *error.
bool upload(StrataEngine* engine, const Plan& plan, Uploaded& out, StrataStatus* status, std::string* error);

// Removes the meshes, then destroys the textures. Ids the engine no longer
// knows are skipped.
void remove(StrataEngine* engine, const Uploaded& uploaded);

}  // namespace strata::gltf
