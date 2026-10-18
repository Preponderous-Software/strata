// SDL3 window with its own OpenGL 4.5 core context.
#include <SDL3/SDL.h>

#include <cstring>

#include "core/error.hpp"
#include "core/log.hpp"
#include "platform/surface.hpp"

namespace strata::platform {
namespace {

class SdlSurface final : public Surface {
public:
    SdlSurface() = default;

    // Separate from the constructor so that a failure part-way still runs the
    // destructor and releases whatever was created.
    void init(const StrataEngineDesc& desc) {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            fail(STRATA_ERROR_PLATFORM, "SDL_InitSubSystem(VIDEO) failed: %s", SDL_GetError());
        }
        videoInitialised_ = true;

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
        if (desc.flags & STRATA_FLAG_DEBUG_CONTEXT) {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
        }

        SDL_WindowFlags windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (desc.flags & STRATA_FLAG_HIDDEN_WINDOW) {
            windowFlags |= SDL_WINDOW_HIDDEN;
        }
        const char* title = desc.title ? desc.title : "Strata";
        window_ = SDL_CreateWindow(title, desc.width, desc.height, windowFlags);
        if (!window_) {
            fail(STRATA_ERROR_PLATFORM, "SDL_CreateWindow failed: %s", SDL_GetError());
        }
        context_ = SDL_GL_CreateContext(window_);
        if (!context_) {
            fail(STRATA_ERROR_PLATFORM, "could not create an OpenGL 4.5 core context: %s", SDL_GetError());
        }
        if (!SDL_GL_MakeCurrent(window_, context_)) {
            fail(STRATA_ERROR_PLATFORM, "SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        }
        SDL_GL_SetSwapInterval((desc.flags & STRATA_FLAG_VSYNC) ? 1 : 0);
        // Text entry (names, console commands) arrives as TEXT_INPUT events.
        SDL_StartTextInput(window_);
        logf(LogLevel::Info, "window surface: SDL %d.%d.%d, video driver %s", SDL_VERSIONNUM_MAJOR(SDL_GetVersion()),
             SDL_VERSIONNUM_MINOR(SDL_GetVersion()), SDL_VERSIONNUM_MICRO(SDL_GetVersion()),
             SDL_GetCurrentVideoDriver());
    }

    ~SdlSurface() override {
        if (context_) SDL_GL_DestroyContext(context_);
        if (window_) SDL_DestroyWindow(window_);
        if (videoInitialised_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    void* procAddress(const char* name) override { return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name)); }
    bool needsOffscreenTarget() const override { return false; }

    bool pollEvent(StrataEvent& out) override {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            std::memset(&out, 0, sizeof out);
            if (translate(event, out)) {
                return true;
            }
        }
        return false;
    }

    void drawableSize(int& width, int& height) const override {
        if (!SDL_GetWindowSizeInPixels(window_, &width, &height)) {
            width = height = 0;
        }
    }

    void resize(int width, int height) override { SDL_SetWindowSize(window_, width, height); }
    void present() override { SDL_GL_SwapWindow(window_); }
    const char* kind() const override { return "window"; }

private:
    static bool translate(const SDL_Event& event, StrataEvent& out) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                out.type = STRATA_EVENT_QUIT;
                return true;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                out.type = event.type == SDL_EVENT_KEY_DOWN ? STRATA_EVENT_KEY_DOWN : STRATA_EVENT_KEY_UP;
                out.scancode = static_cast<int32_t>(event.key.scancode);
                out.modifiers = static_cast<int32_t>(event.key.mod);
                out.repeat = event.key.repeat ? 1 : 0;
                return true;
            case SDL_EVENT_MOUSE_MOTION:
                out.type = STRATA_EVENT_MOUSE_MOTION;
                out.x = event.motion.x;
                out.y = event.motion.y;
                out.dx = event.motion.xrel;
                out.dy = event.motion.yrel;
                return true;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                out.type = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? STRATA_EVENT_MOUSE_BUTTON_DOWN
                                                                     : STRATA_EVENT_MOUSE_BUTTON_UP;
                out.button = event.button.button;
                out.x = event.button.x;
                out.y = event.button.y;
                return true;
            case SDL_EVENT_MOUSE_WHEEL:
                out.type = STRATA_EVENT_MOUSE_WHEEL;
                out.x = event.wheel.x;
                out.y = event.wheel.y;
                return true;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                out.type = STRATA_EVENT_RESIZED;
                out.width = event.window.data1;
                out.height = event.window.data2;
                return true;
            case SDL_EVENT_TEXT_INPUT:
                out.type = STRATA_EVENT_TEXT_INPUT;
                if (event.text.text) {
                    std::strncpy(out.text, event.text.text, sizeof out.text - 1);
                }
                return true;
            default:
                return false;
        }
    }

    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    bool videoInitialised_ = false;
};

}  // namespace

std::unique_ptr<Surface> createSdlSurface(const StrataEngineDesc& desc) {
    auto surface = std::make_unique<SdlSurface>();
    surface->init(desc);
    return surface;
}

}  // namespace strata::platform
