// The host already owns a window and a current GL context; Strata only draws
// into it. Used by Beyond Nations phase B1, where Silk.NET keeps the window.
#include "core/error.hpp"
#include "platform/surface.hpp"

namespace strata::platform {
namespace {

class HostSurface final : public Surface {
public:
    explicit HostSurface(const StrataEngineDesc& desc)
        : getProc_(desc.get_proc_address), user_(desc.get_proc_address_user), width_(desc.width),
          height_(desc.height) {}

    void* procAddress(const char* name) override { return getProc_(name, user_); }
    bool needsOffscreenTarget() const override { return false; }
    bool isHostOwned() const override { return true; }

    void drawableSize(int& width, int& height) const override {
        width = width_;
        height = height_;
    }
    void resize(int width, int height) override {
        width_ = width;
        height_ = height;
    }
    const char* kind() const override { return "host-context"; }

private:
    StrataGetProcAddressFn getProc_;
    void* user_;
    int width_;
    int height_;
};

}  // namespace

std::unique_ptr<Surface> createHostSurface(const StrataEngineDesc& desc) {
    if (!desc.get_proc_address) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "a host-context surface needs get_proc_address");
    }
    return std::make_unique<HostSurface>(desc);
}

}  // namespace strata::platform
