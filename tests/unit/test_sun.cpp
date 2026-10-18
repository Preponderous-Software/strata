#include "render/sun.hpp"
#include "test.hpp"

using namespace strata::render;

TEST(sun_rises_east_peaks_at_noon_and_sets_west) {
    const glm::vec3 rise = sunDirection(0.25f, 35.0f);
    CHECK_NEAR(rise.x, 1.0, 1e-5);
    CHECK_NEAR(rise.y, 0.0, 1e-5);
    const glm::vec3 set = sunDirection(0.75f, 35.0f);
    CHECK_NEAR(set.x, -1.0, 1e-5);
    const glm::vec3 noon = sunDirection(0.5f, 35.0f);
    CHECK_NEAR(noon.y, std::cos(glm::radians(35.0f)), 1e-5);
    CHECK(noon.z > 0.0f);  // northern latitude: the noon sun is to the south (+Z)
    CHECK(sunDirection(0.4f, 35.0f).y < noon.y);
    CHECK(sunDirection(0.6f, 35.0f).y < noon.y);
}

TEST(equator_noon_sun_is_overhead) {
    const glm::vec3 noon = sunDirection(0.5f, 0.0f);
    CHECK_NEAR(noon.y, 1.0, 1e-5);
}

TEST(time_of_day_wraps) {
    const glm::vec3 a = sunDirection(0.3f, 20.0f), b = sunDirection(1.3f, 20.0f);
    CHECK_NEAR(a.x, b.x, 1e-5);
    CHECK_NEAR(a.y, b.y, 1e-5);
}

TEST(night_is_moonlit_and_casts_no_shadow) {
    const SkyState night = evaluateSky(0.95f, 35.0f);
    CHECK(night.lightIsMoon);
    CHECK(night.shadowStrength == 0.0f);
    CHECK(night.lightColor.x < 0.2f);
    CHECK(night.toLight.y > 0.0f);
    const SkyState noon = evaluateSky(0.5f, 35.0f);
    CHECK(!noon.lightIsMoon);
    CHECK_NEAR(noon.shadowStrength, 1.0, 1e-6);
    CHECK(noon.zenith.z > night.zenith.z);
}

TEST(low_sun_fades_its_shadows_out) {
    CHECK(evaluateSky(0.251f, 35.0f).shadowStrength == 0.0f);
    const float morning = evaluateSky(0.30f, 35.0f).shadowStrength;
    CHECK(morning > 0.0f);
    CHECK(morning <= 1.0f);
}
