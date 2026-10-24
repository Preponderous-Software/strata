// strata.hpp -- Strata's C++ API.
//
// This is the engine's real interface; the C ABI in strata.h is a thin shim
// over it. A C++ game (the colony sim) links the static library target
// `strata::static` and uses this header directly. The shared library exports
// only the C ABI.
//
// Data types are the C ABI's plain structs, so there is one definition of a
// render item, a camera or a lighting setup, not two that can drift.
//
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "strata/strata.h"

namespace strata {

// Thrown by every Engine member that can fail.
class Error : public std::runtime_error {
public:
    Error(StrataStatus status, const std::string& message) : std::runtime_error(message), status_(status) {}
    StrataStatus status() const { return status_; }

private:
    StrataStatus status_;
};

struct EngineDesc {
    int32_t surface = STRATA_SURFACE_WINDOW;
    int32_t width = 1280;
    int32_t height = 720;
    uint32_t flags = 0;
    std::string title = "Strata";
    StrataGetProcAddressFn getProcAddress = nullptr;  // host context only
    void* getProcAddressUser = nullptr;
};

class Engine {
public:
    // Creates the surface (window, headless context or the host's context),
    // loads GL 4.5 core and builds the renderer. Throws strata::Error.
    static std::unique_ptr<Engine> create(const EngineDesc& desc);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    const std::string& gpuDescription() const;

    bool pollEvent(StrataEvent& out);
    void drawableSize(int32_t& width, int32_t& height) const;
    void resize(int32_t width, int32_t height);

    void setCamera(const StrataCamera& camera);
    void setLighting(const StrataLighting& lighting);
    // ABI 1.6. Places the sun (a vector towards it, world space); nullptr goes
    // back to the lighting's time_of_day and latitude_degrees.
    void setSunDirection(const float* toSun);
    // Replaces the point lights (copied). count 0 removes them; at most
    // STRATA_MAX_POINT_LIGHTS. Validates every light before changing any.
    void setPointLights(const StrataPointLight* lights, size_t count);
    // Replaces the terrain tile (copied into a mesh); nullptr removes it.
    void setTerrain(const StrataTerrainTile* tile);
    // Adds a static mesh (copied); returns its id, never 0. Validates first;
    // at most STRATA_MAX_MESHES.
    uint32_t addMesh(const StrataMesh& mesh);
    // Removes one mesh; throws INVALID_ARGUMENT for an unknown id.
    void removeMesh(uint32_t id);
    void clearMeshes();
    // ABI 1.7. Textures (RGBA8, copied; STRATA_TEXTURE_* flags), textured
    // meshes and per-mesh model matrices (16 floats, column-major; nullptr
    // restores the identity).
    uint32_t createTextureRgba8(int32_t width, int32_t height, const uint8_t* rgba, uint32_t flags);
    void destroyTexture(uint32_t id);
    uint32_t addTexturedMesh(const StrataTexturedMesh& mesh);
    void setMeshTransform(uint32_t id, const float* model);
    // ABI 1.8. Replaces a width x height rectangle at (x, y) of a texture
    // (RGBA8, rows top to bottom, copied); rebuilds its mip chain if it has
    // one. Throws INVALID_ARGUMENT for an unknown id or a rectangle outside
    // the texture, and then changes nothing.
    void updateTextureRgba8(uint32_t id, int32_t x, int32_t y, int32_t width, int32_t height, const uint8_t* rgba);
    // ABI 1.8. The nearest static mesh triangle along a ray (world space;
    // direction finite and non-zero, normalised here; maxDistance > 0), both
    // faces counting. mesh_id is 0 when nothing is hit. See
    // strata_raycast_meshes for the memory this costs.
    StrataRayHit raycastMeshes(const float origin[3], const float direction[3], float maxDistance) const;
    // ABI 1.8. Hides and captures the cursor so mouse motion arrives as dx/dy
    // only (first-person look). Window surface only: throws UNSUPPORTED on
    // headless and host-context surfaces.
    void setRelativeMouseMode(bool enabled);

    void beginFrame(const StrataFrameDesc& desc);
    void submitPrimitives(const StrataRenderItem* items, size_t count);
    // Overlays (ABI 1.5): unlit lines, points and screen text for this frame.
    void submitLines(const StrataLineVertex* vertices, size_t count);
    void submitPoints(const StrataPoint* points, size_t count);
    void submitText(const StrataText* texts, size_t count);
    // ABI 1.11: lines with STRATA_LINES_* flags, and filled screen rectangles.
    void submitLines(const StrataLineVertex* vertices, size_t count, uint32_t flags);
    void submitRects(const StrataRect* rects, size_t count);
    void endFrame();
    void present();

    const StrataFrameStats& frameStats() const;

    // RGBA8, top row first.
    void readPixels(std::vector<uint8_t>& rgba, int32_t& width, int32_t& height);
    // ABI 1.9. The last frame's depth: planar distance along the view axis in
    // world units, top row first, +infinity where nothing was drawn. Headless
    // surfaces only: throws UNSUPPORTED on a window or host context.
    void readDepth(std::vector<float>& depth, int32_t& width, int32_t& height);
    void saveScreenshotPng(const std::string& path);

    struct Impl;

private:
    explicit Engine(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// Defaults for a lighting mode. LEGACY returns Beyond Nations'
// PrimitiveRenderer constants.
StrataLighting lightingDefaults(int32_t mode);

// ABI 1.6. The sun's position at a moment (seconds since 1970-01-01 UTC) and
// place (degrees, north and east positive). Throws INVALID_ARGUMENT.
StrataSolarPosition solarPosition(double unixSecondsUtc, double latitudeDegrees, double longitudeDegrees);

// Right-handed look-at view and an OpenGL (depth -1..1) perspective.
StrataCamera cameraLookAt(const float eye[3], const float target[3], const float up[3], float fovYDegrees,
                          float aspect, float nearPlane, float farPlane);

}  // namespace strata
