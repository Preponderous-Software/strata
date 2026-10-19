// Headless OpenGL 4.5 core (or OpenGL ES 3.0, for testing the WebGL 2 device
// natively) through EGL, with no window and no X server.
//
// libEGL is opened at run time by glad's loader, so libstrata has no link-time
// dependency on it: a desktop install that never asks for a headless surface
// never needs EGL at all.
//
// Display selection, first match wins:
//   1. EGL_MESA_platform_surfaceless  (Mesa; what CI uses with llvmpipe)
//   2. EGL_EXT_platform_device        (first enumerated device)
//   3. the default display            (whatever libEGL offers)
// On Mesa, LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe selects llvmpipe.
// With libglvnd, __EGL_VENDOR_LIBRARY_FILENAMES can force the Mesa vendor.
#include <glad/egl.h>

#include <cstring>
#include <vector>

#include "core/error.hpp"
#include "core/log.hpp"
#include "platform/surface.hpp"

namespace strata::platform {
namespace {

bool hasExtension(const char* list, const char* name) {
    if (!list) return false;
    const size_t length = std::strlen(name);
    for (const char* p = list; (p = std::strstr(p, name)) != nullptr; p += length) {
        const bool startOk = p == list || p[-1] == ' ';
        const bool endOk = p[length] == ' ' || p[length] == '\0';
        if (startOk && endOk) return true;
    }
    return false;
}

class EglSurface final : public Surface {
public:
    EglSurface() = default;

    void init(const StrataEngineDesc& desc, GlApi api) {
        gles_ = api == GlApi::Gles3;
        width_ = desc.width;
        height_ = desc.height;

        if (!gladLoaderLoadEGL(EGL_NO_DISPLAY)) {
            fail(STRATA_ERROR_PLATFORM, "could not load libEGL (is an EGL implementation such as Mesa installed?)");
        }
        loaderLoaded_ = true;

        const char* clientExtensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
        const char* how = nullptr;
        if (hasExtension(clientExtensions, "EGL_MESA_platform_surfaceless")) {
            display_ = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY);
            how = "surfaceless";
        }
        if (display_ == EGL_NO_DISPLAY && hasExtension(clientExtensions, "EGL_EXT_platform_device") &&
            eglQueryDevicesEXT) {
            EGLDeviceEXT devices[8];
            EGLint count = 0;
            if (eglQueryDevicesEXT(8, devices, &count) && count > 0) {
                display_ = getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, devices[0]);
                how = "device";
            }
        }
        if (display_ == EGL_NO_DISPLAY) {
            display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
            how = "default";
        }
        if (display_ == EGL_NO_DISPLAY) {
            fail(STRATA_ERROR_PLATFORM, "no EGL display is available");
        }

        EGLint major = 0, minor = 0;
        if (!eglInitialize(display_, &major, &minor)) {
            display_ = EGL_NO_DISPLAY;
            fail(STRATA_ERROR_PLATFORM, "eglInitialize failed (0x%x)", static_cast<unsigned>(eglGetError()));
        }
        displayInitialised_ = true;
        // Reload now that the display's version and extensions are known.
        gladLoaderLoadEGL(display_);

        if (!eglBindAPI(gles_ ? EGL_OPENGL_ES_API : EGL_OPENGL_API)) {
            fail(STRATA_ERROR_PLATFORM, "this EGL cannot bind %s", gles_ ? "OpenGL ES" : "desktop OpenGL");
        }

        EGLConfig config = chooseConfig();

        std::vector<EGLint> contextAttribs;
        if (gles_) {
            contextAttribs = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0};
        } else {
            contextAttribs = {
                EGL_CONTEXT_MAJOR_VERSION, 4,
                EGL_CONTEXT_MINOR_VERSION, 5,
                EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
            };
        }
        if (desc.flags & STRATA_FLAG_DEBUG_CONTEXT) {
            contextAttribs.insert(contextAttribs.end(), {EGL_CONTEXT_OPENGL_DEBUG, EGL_TRUE});
        }
        contextAttribs.push_back(EGL_NONE);
        context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, contextAttribs.data());
        if (context_ == EGL_NO_CONTEXT) {
            fail(STRATA_ERROR_PLATFORM, "could not create an %s context through EGL (0x%x)",
                 gles_ ? "OpenGL ES 3.0" : "OpenGL 4.5 core", static_cast<unsigned>(eglGetError()));
        }
        if (!eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_)) {
            fail(STRATA_ERROR_PLATFORM, "eglMakeCurrent without a surface failed (0x%x)",
                 static_cast<unsigned>(eglGetError()));
        }
        logf(LogLevel::Info, "headless surface: EGL %d.%d, %s display, vendor %s", major, minor, how,
             eglQueryString(display_, EGL_VENDOR));
    }

    ~EglSurface() override {
        if (display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
            if (displayInitialised_) eglTerminate(display_);
        }
        // libEGL stays loaded for the life of the process: Mesa keeps worker
        // threads inside it, and dlclose under them crashes the next engine.
    }

    void* procAddress(const char* name) override { return reinterpret_cast<void*>(eglGetProcAddress(name)); }
    bool needsOffscreenTarget() const override { return true; }

    void drawableSize(int& width, int& height) const override {
        width = width_;
        height = height_;
    }
    void resize(int width, int height) override {
        width_ = width;
        height_ = height;
    }
    const char* kind() const override { return "headless"; }

private:
    EGLDisplay getPlatformDisplay(EGLenum platform, void* nativeDisplay) {
        if (eglGetPlatformDisplay) {
            return eglGetPlatformDisplay(platform, nativeDisplay, nullptr);
        }
        if (eglGetPlatformDisplayEXT) {
            return eglGetPlatformDisplayEXT(platform, nativeDisplay, nullptr);
        }
        return EGL_NO_DISPLAY;
    }

    EGLConfig chooseConfig() {
        // Surfaceless displays expose no window-capable configs, and
        // EGL_SURFACE_TYPE defaults to EGL_WINDOW_BIT, so ask for pbuffer and
        // then for anything at all.
        const EGLint surfaceTypes[] = {EGL_PBUFFER_BIT, EGL_DONT_CARE};
        for (EGLint surfaceType : surfaceTypes) {
            const EGLint attribs[] = {EGL_RENDERABLE_TYPE, gles_ ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_BIT,
                                      EGL_SURFACE_TYPE, surfaceType, EGL_NONE};
            EGLConfig config = nullptr;
            EGLint count = 0;
            if (eglChooseConfig(display_, attribs, &config, 1, &count) && count > 0) {
                return config;
            }
        }
        fail(STRATA_ERROR_PLATFORM, "no EGL config can render %s", gles_ ? "OpenGL ES 3" : "desktop OpenGL");
    }

    bool gles_ = false;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    bool loaderLoaded_ = false;
    bool displayInitialised_ = false;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace

std::unique_ptr<Surface> createEglSurface(const StrataEngineDesc& desc, GlApi api) {
    auto surface = std::make_unique<EglSurface>();
    surface->init(desc, api);
    return surface;
}

}  // namespace strata::platform
