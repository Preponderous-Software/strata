#if !defined(__EMSCRIPTEN__)
#include <glad/gl.h>
#endif

#include <chrono>
#include <cmath>
#include <cstring>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "core/error.hpp"
#include "core/log.hpp"
#include "image/image.hpp"
#include "platform/surface.hpp"
#include "render/renderer.hpp"
#include "rhi/gpu_device.hpp"
#include "strata/strata.hpp"

namespace strata {
namespace {

#if !defined(__EMSCRIPTEN__)
GLADapiproc loadThroughSurface(void* user, const char* name) {
    auto* surface = static_cast<platform::Surface*>(user);
    return reinterpret_cast<GLADapiproc>(surface->procAddress(name));
}
#endif

}  // namespace

struct Engine::Impl {
    EngineDesc desc;
    std::unique_ptr<platform::Surface> surface;
    std::unique_ptr<rhi::GpuDevice> device;
    std::unique_ptr<render::Renderer> renderer;
    std::string gpuDescription;
    StrataFrameStats stats{};

    ~Impl() {
        // GL objects go before the context that owns them.
        renderer.reset();
        device.reset();
        surface.reset();
    }
};

std::unique_ptr<Engine> Engine::create(const EngineDesc& desc) {
    if (desc.width <= 0 || desc.height <= 0 || desc.width > 16384 || desc.height > 16384) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "drawable size must be 1..16384 on each side (got %dx%d)", desc.width,
             desc.height);
    }
    auto impl = std::make_unique<Impl>();
    impl->desc = desc;

    StrataEngineDesc raw{};
    raw.struct_size = sizeof raw;
    raw.abi_version = STRATA_ABI_VERSION;
    raw.surface = desc.surface;
    raw.width = desc.width;
    raw.height = desc.height;
    raw.flags = desc.flags;
    raw.title = desc.title.c_str();
    raw.get_proc_address = desc.getProcAddress;
    raw.get_proc_address_user = desc.getProcAddressUser;
    const platform::GlApi api = platform::requestedGlApi();
    impl->surface = platform::createSurface(raw, api);
    const bool debug = (desc.flags & STRATA_FLAG_DEBUG_CONTEXT) != 0;

#if defined(__EMSCRIPTEN__)
    // Emscripten's WebGL 2 bindings need no loader.
    impl->device = rhi::createGles3Device(debug);
    impl->gpuDescription = impl->device->description();
    logf(LogLevel::Info, "%s surface, WebGL 2: %s", impl->surface->kind(), impl->gpuDescription.c_str());
#else
    if (api == platform::GlApi::Gles3) {
        const int version = gladLoadGLES2UserPtr(loadThroughSurface, impl->surface.get());
        if (version == 0) {
            fail(STRATA_ERROR_GPU, "could not load OpenGL ES entry points through the %s surface",
                 impl->surface->kind());
        }
        const int major = GLAD_VERSION_MAJOR(version), minor = GLAD_VERSION_MINOR(version);
        if (major < 3) fail(STRATA_ERROR_GPU, "Strata's GLES device needs OpenGL ES 3.0; this context is %d.%d", major, minor);
        impl->device = rhi::createGles3Device(debug);
        impl->gpuDescription = impl->device->description();
        logf(LogLevel::Info, "%s surface, GLES %d.%d: %s", impl->surface->kind(), major, minor,
             impl->gpuDescription.c_str());
    } else {
        const int version = gladLoadGLUserPtr(loadThroughSurface, impl->surface.get());
        if (version == 0) {
            fail(STRATA_ERROR_GPU, "could not load OpenGL entry points through the %s surface", impl->surface->kind());
        }
        const int major = GLAD_VERSION_MAJOR(version), minor = GLAD_VERSION_MINOR(version);
        if (major < 4 || (major == 4 && minor < 5)) {
            fail(STRATA_ERROR_GPU, "Strata needs OpenGL 4.5 core; this context is %d.%d", major, minor);
        }
        impl->device = rhi::createGl45Device(debug);
        impl->gpuDescription = impl->device->description();
        logf(LogLevel::Info, "%s surface, GL %d.%d: %s", impl->surface->kind(), major, minor,
             impl->gpuDescription.c_str());
    }
#endif
    impl->renderer = std::make_unique<render::Renderer>(*impl->device, *impl->surface);
    return std::unique_ptr<Engine>(new Engine(std::move(impl)));
}

Engine::Engine(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Engine::~Engine() = default;

const std::string& Engine::gpuDescription() const { return impl_->gpuDescription; }

bool Engine::pollEvent(StrataEvent& out) { return impl_->surface->pollEvent(out); }

void Engine::drawableSize(int32_t& width, int32_t& height) const {
    int w = 0, h = 0;
    impl_->surface->drawableSize(w, h);
    width = w;
    height = h;
}

void Engine::resize(int32_t width, int32_t height) {
    if (width < 0 || height < 0 || width > 16384 || height > 16384) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "drawable size must be 0..16384 on each side (got %dx%d)", width, height);
    }
    impl_->surface->resize(width, height);
}

void Engine::setCamera(const StrataCamera& camera) { impl_->renderer->setCamera(camera); }
void Engine::setLighting(const StrataLighting& lighting) { impl_->renderer->setLighting(lighting); }
void Engine::setSunDirection(const float* toSun) { impl_->renderer->setSunDirection(toSun); }

void Engine::setTerrain(const StrataTerrainTile* tile) { impl_->renderer->setTerrain(tile); }

uint32_t Engine::addMesh(const StrataMesh& mesh) { return impl_->renderer->addMesh(mesh); }
void Engine::removeMesh(uint32_t id) { impl_->renderer->removeMesh(id); }
void Engine::clearMeshes() { impl_->renderer->clearMeshes(); }
uint32_t Engine::createTextureRgba8(int32_t width, int32_t height, const uint8_t* rgba, uint32_t flags) {
    return impl_->renderer->createTexture(width, height, rgba, flags);
}
void Engine::destroyTexture(uint32_t id) { impl_->renderer->destroyTexture(id); }
uint32_t Engine::addTexturedMesh(const StrataTexturedMesh& mesh) { return impl_->renderer->addTexturedMesh(mesh); }
void Engine::setMeshTransform(uint32_t id, const float* model) { impl_->renderer->setMeshTransform(id, model); }
void Engine::updateTextureRgba8(uint32_t id, int32_t x, int32_t y, int32_t width, int32_t height,
                                const uint8_t* rgba) {
    impl_->renderer->updateTexture(id, x, y, width, height, rgba);
}
StrataRayHit Engine::raycastMeshes(const float origin[3], const float direction[3], float maxDistance) const {
    return impl_->renderer->raycastMeshes(origin, direction, maxDistance);
}
void Engine::setRelativeMouseMode(bool enabled) { impl_->surface->setRelativeMouseMode(enabled); }

void Engine::setPointLights(const StrataPointLight* lights, size_t count) {
    if (count > 0 && lights == nullptr) fail(STRATA_ERROR_INVALID_ARGUMENT, "lights is NULL with a non-zero count");
    impl_->renderer->setPointLights(lights, count);
}
void Engine::beginFrame(const StrataFrameDesc& desc) { impl_->renderer->beginFrame(desc); }

void Engine::submitPrimitives(const StrataRenderItem* items, size_t count) {
    if (count > 0 && items == nullptr) fail(STRATA_ERROR_INVALID_ARGUMENT, "items is NULL with a non-zero count");
    impl_->renderer->submit(items, count);
}

void Engine::submitLines(const StrataLineVertex* vertices, size_t count) {
    if (count > 0 && vertices == nullptr) fail(STRATA_ERROR_INVALID_ARGUMENT, "vertices is NULL with a non-zero count");
    impl_->renderer->submitLines(vertices, count);
}

void Engine::submitPoints(const StrataPoint* points, size_t count) {
    if (count > 0 && points == nullptr) fail(STRATA_ERROR_INVALID_ARGUMENT, "points is NULL with a non-zero count");
    impl_->renderer->submitPoints(points, count);
}

void Engine::submitText(const StrataText* texts, size_t count) {
    if (count > 0 && texts == nullptr) fail(STRATA_ERROR_INVALID_ARGUMENT, "texts is NULL with a non-zero count");
    impl_->renderer->submitText(texts, count);
}

void Engine::endFrame() {
    impl_->renderer->endFrame();
    impl_->stats = impl_->renderer->stats();
    if (impl_->desc.flags & STRATA_FLAG_GPU_TIMING) {
        const auto start = std::chrono::steady_clock::now();
        impl_->device->finish();
        impl_->stats.gpu_finish_ms =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
}

void Engine::present() { impl_->surface->present(); }

const StrataFrameStats& Engine::frameStats() const { return impl_->stats; }

void Engine::readPixels(std::vector<uint8_t>& rgba, int32_t& width, int32_t& height) {
    int w = 0, h = 0;
    impl_->renderer->readPixels(rgba, w, h);
    width = w;
    height = h;
}

void Engine::saveScreenshotPng(const std::string& path) {
    std::vector<uint8_t> rgba;
    int32_t width = 0, height = 0;
    readPixels(rgba, width, height);
    image::Image img;
    img.width = width;
    img.height = height;
    img.rgba = std::move(rgba);
    // Screenshots are opaque: alpha would only carry whatever the clear left.
    for (size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
    std::string error;
    if (!image::writePng(path, img, &error)) {
        fail(STRATA_ERROR_IO, "could not write %s: %s", path.c_str(), error.c_str());
    }
}

StrataSolarPosition solarPosition(double unixSecondsUtc, double latitudeDegrees, double longitudeDegrees) {
    if (!std::isfinite(unixSecondsUtc) || !std::isfinite(latitudeDegrees) || !std::isfinite(longitudeDegrees) ||
        std::fabs(latitudeDegrees) > 90.0 || std::fabs(longitudeDegrees) > 180.0) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "solar position needs a finite time, latitude in [-90, 90] and "
                                            "longitude in [-180, 180]");
    }
    const render::SolarPosition p = render::solarPosition(unixSecondsUtc, latitudeDegrees, longitudeDegrees);
    StrataSolarPosition out{};
    out.to_sun[0] = p.toSun.x;
    out.to_sun[1] = p.toSun.y;
    out.to_sun[2] = p.toSun.z;
    out.elevation_degrees = static_cast<float>(p.elevationDegrees);
    out.azimuth_degrees = static_cast<float>(p.azimuthDegrees);
    out.declination_degrees = static_cast<float>(p.declinationDegrees);
    out.hour_angle_degrees = static_cast<float>(p.hourAngleDegrees);
    out.equation_of_time_minutes = static_cast<float>(p.equationOfTimeMinutes);
    return out;
}

StrataLighting lightingDefaults(int32_t mode) {
    StrataLighting l{};
    l.mode = mode;
    // BN's PrimitiveRenderer: LightDirection = normalize(-0.35, -0.85, -0.4), Ambient = 0.35.
    const glm::vec3 d = glm::normalize(glm::vec3(-0.35f, -0.85f, -0.4f));
    l.legacy_direction[0] = d.x;
    l.legacy_direction[1] = d.y;
    l.legacy_direction[2] = d.z;
    l.legacy_ambient = 0.35f;
    l.time_of_day = 0.4f;
    l.latitude_degrees = 35.0f;
    l.cascade_count = 4;
    l.shadow_map_size = 2048;
    l.shadow_distance = 150.0f;
    l.split_lambda = 0.6f;
    l.fog = 1;
    if (mode == STRATA_LIGHTING_SPACE) {
        // A faint fill so night sides are not pure black; the fields the
        // mode ignores keep the SUN defaults.
        l.legacy_ambient = 0.03f;
        l.fog = 0;
    }
    return l;
}

StrataCamera cameraLookAt(const float eye[3], const float target[3], const float up[3], float fovYDegrees,
                          float aspect, float nearPlane, float farPlane) {
    StrataCamera c{};
    const glm::vec3 e(eye[0], eye[1], eye[2]);
    const glm::mat4 view = glm::lookAt(e, glm::vec3(target[0], target[1], target[2]), glm::vec3(up[0], up[1], up[2]));
    const glm::mat4 projection = glm::perspective(glm::radians(fovYDegrees), aspect, nearPlane, farPlane);
    std::memcpy(c.view, glm::value_ptr(view), sizeof c.view);
    std::memcpy(c.projection, glm::value_ptr(projection), sizeof c.projection);
    c.eye[0] = e.x;
    c.eye[1] = e.y;
    c.eye[2] = e.z;
    c.near_plane = nearPlane;
    c.far_plane = farPlane;
    return c;
}

}  // namespace strata
