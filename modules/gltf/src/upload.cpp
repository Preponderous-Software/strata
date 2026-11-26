#include "strata/gltf/upload.hpp"

namespace strata::gltf {

bool upload(StrataEngine* engine, const Plan& plan, Uploaded& out, StrataStatus* status, std::string* error) {
    out = Uploaded{};
    auto fail = [&](StrataStatus s, const std::string& what) {
        if (status) *status = s;
        if (error) *error = what + ": " + strata_last_error();
        remove(engine, out);
        out = Uploaded{};
        return false;
    };
    for (const PlannedTexture& t : plan.textures) {
        uint32_t id = 0;
        const StrataStatus s = strata_create_texture_rgba8(engine, t.width, t.height, t.rgba.data(), t.flags, &id);
        if (s != STRATA_OK) return fail(s, "strata_create_texture_rgba8 (" + t.label + ")");
        out.textureIds.push_back(id);
    }
    for (const PlannedMesh& m : plan.meshes) {
        StrataTexturedMesh tm{};
        tm.mesh.positions = m.positions.data();
        tm.mesh.normals = m.normals.data();
        tm.mesh.colors = m.colors.data();
        tm.mesh.indices = m.indices.data();
        tm.mesh.vertex_count = static_cast<int32_t>(m.vertexCount());
        tm.mesh.index_count = static_cast<int32_t>(m.indices.size());
        for (int k = 0; k < 4; ++k) tm.mesh.color[k] = m.color[k];
        tm.uvs = m.texture >= 0 ? m.uvs.data() : nullptr;
        tm.texture = m.texture >= 0 ? out.textureIds[static_cast<size_t>(m.texture)] : 0u;
        tm.flags = m.flags;
        uint32_t id = 0;
        const StrataStatus s = strata_add_textured_mesh(engine, &tm, &id);
        if (s != STRATA_OK) return fail(s, "strata_add_textured_mesh (" + m.label + ")");
        out.meshIds.push_back(id);
    }
    if (status) *status = STRATA_OK;
    return true;
}

void remove(StrataEngine* engine, const Uploaded& uploaded) {
    if (!engine) return;
    for (uint32_t id : uploaded.meshIds) strata_remove_mesh(engine, id);
    for (uint32_t id : uploaded.textureIds) strata_destroy_texture(engine, id);
}

}  // namespace strata::gltf
