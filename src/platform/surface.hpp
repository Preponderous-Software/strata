// The platform layer: where a GL context comes from (GL 4.5 core, or GLES 3.0
// for WebGL 2), where input comes from, and where finished frames go.
//
// Three implementations sit behind one interface:
//   SdlSurface   an SDL3 window with its own context (games, Xvfb fallback)
//   EglSurface   an EGL surfaceless context with no window (CI, golden images)
//   HostSurface  the context the host already made current (BN phase B1,
//                where Silk.NET still owns the window)
#pragma once

#include <memory>
#include <string>

#include "strata/strata.h"

namespace strata::platform {

// Which GL a surface creates (or, for a host context, which GL the host made).
enum class GlApi {
    Gl45,   // desktop OpenGL 4.5 core: the default
    Gles3,  // OpenGL ES 3.0: always under Emscripten (WebGL 2); natively when
            // the environment sets STRATA_GL_API=gles3 (tests, and GLES-only
            // devices)
};

// The API this process asks for. Throws strata::Error on an unknown
// STRATA_GL_API value.
GlApi requestedGlApi();

class Surface {
public:
    virtual ~Surface() = default;

    // GL entry point lookup for glad. Valid while the context is current.
    virtual void* procAddress(const char* name) = 0;

    // True when there is no default framebuffer worth drawing to, so the
    // renderer must draw into an offscreen target of drawableSize().
    virtual bool needsOffscreenTarget() const = 0;

    // True when the GL context belongs to someone else, whose state the
    // renderer must leave as it found it.
    virtual bool isHostOwned() const { return false; }

    virtual bool pollEvent(StrataEvent& out) {
        (void)out;
        return false;
    }

    virtual void drawableSize(int& width, int& height) const = 0;
    virtual void resize(int width, int height) = 0;
    virtual void present() {}

    virtual const char* kind() const = 0;
};

// Picks the implementation for desc->surface. Throws strata::Error.
std::unique_ptr<Surface> createSurface(const StrataEngineDesc& desc, GlApi api);

std::unique_ptr<Surface> createHostSurface(const StrataEngineDesc& desc);
#if defined(STRATA_HAVE_SDL)
std::unique_ptr<Surface> createSdlSurface(const StrataEngineDesc& desc, GlApi api);
#endif
#if defined(STRATA_HAVE_EGL)
std::unique_ptr<Surface> createEglSurface(const StrataEngineDesc& desc, GlApi api);
#endif

}  // namespace strata::platform
