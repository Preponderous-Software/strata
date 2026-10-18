#include "platform/surface.hpp"

#include "core/error.hpp"
#include "core/log.hpp"

namespace strata::platform {

std::unique_ptr<Surface> createSurface(const StrataEngineDesc& desc) {
    switch (desc.surface) {
        case STRATA_SURFACE_HOST_CONTEXT:
            return createHostSurface(desc);
        case STRATA_SURFACE_WINDOW:
#if defined(STRATA_HAVE_SDL)
            return createSdlSurface(desc);
#else
            fail(STRATA_ERROR_UNSUPPORTED, "this build of Strata has no window surface (built without SDL3)");
#endif
        case STRATA_SURFACE_HEADLESS:
#if defined(STRATA_HAVE_EGL)
            return createEglSurface(desc);
#elif defined(STRATA_HAVE_SDL)
        {
            // No EGL on this platform: a hidden window is the next best thing.
            logf(LogLevel::Info, "headless surface: no EGL in this build, using a hidden SDL3 window");
            StrataEngineDesc hidden = desc;
            hidden.flags |= STRATA_FLAG_HIDDEN_WINDOW;
            return createSdlSurface(hidden);
        }
#else
            fail(STRATA_ERROR_UNSUPPORTED, "this build of Strata has no headless surface");
#endif
        default:
            fail(STRATA_ERROR_INVALID_ARGUMENT, "unknown surface kind %d", desc.surface);
    }
}

}  // namespace strata::platform
