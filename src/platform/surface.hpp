// The platform layer: where a GL 4.5 core context comes from, where input
// comes from, and where finished frames go.
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
std::unique_ptr<Surface> createSurface(const StrataEngineDesc& desc);

std::unique_ptr<Surface> createHostSurface(const StrataEngineDesc& desc);
#if defined(STRATA_HAVE_SDL)
std::unique_ptr<Surface> createSdlSurface(const StrataEngineDesc& desc);
#endif
#if defined(STRATA_HAVE_EGL)
std::unique_ptr<Surface> createEglSurface(const StrataEngineDesc& desc);
#endif

}  // namespace strata::platform
