#include "platform/surface.hpp"

#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "core/error.hpp"
#include "core/log.hpp"

namespace strata::platform {

GlApi requestedGlApi() {
#if defined(__EMSCRIPTEN__)
    return GlApi::Gles3;
#else
    const char* value = std::getenv("STRATA_GL_API");
    if (!value || value[0] == '\0' || std::strcmp(value, "gl45") == 0) return GlApi::Gl45;
    if (std::strcmp(value, "gles3") == 0) return GlApi::Gles3;
    fail(STRATA_ERROR_INVALID_ARGUMENT, "STRATA_GL_API must be gl45 or gles3 (got '%s')", value);
#endif
}

const char* headlessMesaDriverOverride(bool has_dxg, const char* (*env)(const char* name)) {
    if (!has_dxg) return nullptr;
    for (const char* chosen : {"GALLIUM_DRIVER", "LIBGL_ALWAYS_SOFTWARE", "MESA_LOADER_DRIVER_OVERRIDE"}) {
        if (env(chosen) != nullptr) return nullptr;
    }
    return "llvmpipe";
}

void Surface::setRelativeMouseMode(bool enabled) {
    (void)enabled;
    fail(STRATA_ERROR_UNSUPPORTED, "relative mouse mode needs a window surface; this one is %s", kind());
}

std::unique_ptr<Surface> createSurface(const StrataEngineDesc& desc, GlApi api) {
    (void)api;  // unused when the build has neither SDL3 nor EGL
    switch (desc.surface) {
        case STRATA_SURFACE_HOST_CONTEXT:
            return createHostSurface(desc);
        case STRATA_SURFACE_WINDOW:
#if defined(STRATA_HAVE_SDL)
            return createSdlSurface(desc, api);
#else
            fail(STRATA_ERROR_UNSUPPORTED, "this build of Strata has no window surface (built without SDL3)");
#endif
        case STRATA_SURFACE_HEADLESS:
#if defined(STRATA_HAVE_EGL)
            return createEglSurface(desc, api);
#elif defined(STRATA_HAVE_SDL)
        {
            // No EGL on this platform: a hidden window is the next best thing.
            logf(LogLevel::Info, "headless surface: no EGL in this build, using a hidden SDL3 window");
            StrataEngineDesc hidden = desc;
            hidden.flags |= STRATA_FLAG_HIDDEN_WINDOW;
            return createSdlSurface(hidden, api);
        }
#else
            fail(STRATA_ERROR_UNSUPPORTED, "this build of Strata has no headless surface");
#endif
        default:
            fail(STRATA_ERROR_INVALID_ARGUMENT, "unknown surface kind %d", desc.surface);
    }
}

}  // namespace strata::platform
