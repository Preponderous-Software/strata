// The headless surface's Mesa driver choice: llvmpipe on WSL2 (/dev/dxg),
// where Mesa's d3d12 driver can segfault inside eglInitialize, unless the
// host has chosen a driver itself. The choice is a pure function so it can be
// checked on any machine, WSL or not.
#include <cstring>
#include <map>
#include <string>

#include "platform/surface.hpp"
#include "test.hpp"

namespace {

std::map<std::string, std::string> g_env;

const char* fakeEnv(const char* name) {
    auto it = g_env.find(name);
    return it == g_env.end() ? nullptr : it->second.c_str();
}

const char* choose(bool hasDxg, std::map<std::string, std::string> env) {
    g_env = std::move(env);
    return strata::platform::headlessMesaDriverOverride(hasDxg, fakeEnv);
}

bool isLlvmpipe(const char* driver) { return driver != nullptr && std::strcmp(driver, "llvmpipe") == 0; }

}  // namespace

TEST(headless_driver_is_left_alone_without_dxg) {
    CHECK(choose(false, {}) == nullptr);
    CHECK(choose(false, {{"GALLIUM_DRIVER", "d3d12"}}) == nullptr);
}

TEST(headless_driver_is_llvmpipe_on_wsl_when_nothing_is_chosen) {
    CHECK(isLlvmpipe(choose(true, {})));
    CHECK(isLlvmpipe(choose(true, {{"DISPLAY", ":0"}, {"STRATA_GL_API", "gles3"}})));
}

TEST(headless_driver_respects_the_hosts_choice_on_wsl) {
    CHECK(choose(true, {{"GALLIUM_DRIVER", "d3d12"}}) == nullptr);
    CHECK(choose(true, {{"GALLIUM_DRIVER", "llvmpipe"}}) == nullptr);
    CHECK(choose(true, {{"LIBGL_ALWAYS_SOFTWARE", "1"}}) == nullptr);
    CHECK(choose(true, {{"LIBGL_ALWAYS_SOFTWARE", "0"}}) == nullptr);
    CHECK(choose(true, {{"MESA_LOADER_DRIVER_OVERRIDE", "zink"}}) == nullptr);
    CHECK(choose(true, {{"GALLIUM_DRIVER", ""}}) == nullptr);
}
