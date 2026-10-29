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

// ABI 1.6: the sun placed by date, time and place. Unix times are UTC.
namespace {
constexpr double kJuneSolstice2026Noon = 1782043200.0;     // 2026-06-21 12:00
constexpr double kDecemberSolstice2026Noon = 1797854400.0; // 2026-12-21 12:00
constexpr double kMarchEquinox2026Noon = 1774008000.0;     // 2026-03-20 12:00
constexpr double kNovember3rd2026Noon = 1793707200.0;      // 2026-11-03 12:00
constexpr double kFebruary11th2026Noon = 1770811200.0;     // 2026-02-11 12:00
}  // namespace

TEST(solstice_declinations_and_noon_heights) {
    const SolarPosition june = solarPosition(kJuneSolstice2026Noon, 40.0, 0.0);
    CHECK_NEAR(june.declinationDegrees, 23.44, 0.02);
    CHECK_NEAR(june.elevationDegrees, 90.0 - 40.0 + 23.44, 0.1);
    CHECK(june.toSun.z > 0.0f);  // to the south at a northern latitude
    const SolarPosition december = solarPosition(kDecemberSolstice2026Noon, 40.0, 0.0);
    CHECK_NEAR(december.declinationDegrees, -23.44, 0.02);
    CHECK_NEAR(december.elevationDegrees, 90.0 - 40.0 - 23.44, 0.1);
    CHECK_NEAR(december.azimuthDegrees, 180.0, 1.0);
    // The southern hemisphere's noon sun is to the north, and high in December.
    const SolarPosition south = solarPosition(kDecemberSolstice2026Noon, -40.0, 0.0);
    CHECK(south.toSun.z < 0.0f);
    CHECK_NEAR(south.elevationDegrees, 90.0 - 40.0 + 23.44, 0.1);
}

TEST(equinox_sun_is_over_the_equator) {
    const SolarPosition p = solarPosition(kMarchEquinox2026Noon, 0.0, 0.0);
    CHECK(std::fabs(p.declinationDegrees) < 0.3);
    CHECK(p.elevationDegrees > 87.5);  // the equation of time puts solar noon about 7.5 minutes after 12:00
    // Six hours later (and earlier) it is near the horizon, due west (and east).
    const SolarPosition evening = solarPosition(kMarchEquinox2026Noon + 6 * 3600.0, 0.0, 0.0);
    CHECK(std::fabs(evening.elevationDegrees) < 2.5);
    CHECK_NEAR(evening.azimuthDegrees, 270.0, 1.0);
    CHECK_NEAR(evening.toSun.x, -1.0, 0.01);
    const SolarPosition morning = solarPosition(kMarchEquinox2026Noon - 6 * 3600.0, 0.0, 0.0);
    CHECK_NEAR(morning.azimuthDegrees, 90.0, 1.0);
}

TEST(equation_of_time_extremes) {
    CHECK_NEAR(solarPosition(kNovember3rd2026Noon, 0.0, 0.0).equationOfTimeMinutes, 16.4, 0.2);
    CHECK_NEAR(solarPosition(kFebruary11th2026Noon, 0.0, 0.0).equationOfTimeMinutes, -14.2, 0.2);
}

TEST(longitude_shifts_solar_time) {
    // 90 degrees east is six hours ahead: its 06:00 UTC sky is Greenwich's noon.
    const SolarPosition greenwich = solarPosition(kJuneSolstice2026Noon, 40.0, 0.0);
    const SolarPosition east = solarPosition(kJuneSolstice2026Noon - 6 * 3600.0, 40.0, 90.0);
    CHECK_NEAR(east.elevationDegrees, greenwich.elevationDegrees, 0.05);
    CHECK_NEAR(east.hourAngleDegrees, greenwich.hourAngleDegrees, 0.05);
    CHECK(std::fabs(greenwich.hourAngleDegrees) < 1.0);
}

TEST(solar_position_vector_agrees_with_its_angles) {
    const SolarPosition p = solarPosition(kJuneSolstice2026Noon - 3 * 3600.0, 51.5, -0.1);  // a June morning
    CHECK_NEAR(glm::length(p.toSun), 1.0, 1e-5);
    CHECK_NEAR(std::asin(p.toSun.y) * 180.0 / 3.14159265358979, p.elevationDegrees, 1e-3);
    CHECK(p.azimuthDegrees > 90.0 && p.azimuthDegrees < 180.0);  // south-east
    CHECK(p.toSun.x > 0.0f && p.toSun.z > 0.0f);
}

TEST(sky_for_a_placed_sun_matches_the_time_of_day_sky) {
    const SkyState a = evaluateSky(0.4f, 35.0f);
    const SkyState b = evaluateSkyForSun(sunDirection(0.4f, 35.0f));
    CHECK_NEAR(a.lightColor.x, b.lightColor.x, 1e-6);
    CHECK_NEAR(a.shadowStrength, b.shadowStrength, 1e-6);
    CHECK(evaluateSkyForSun(solarPosition(kDecemberSolstice2026Noon, 40.0, 0.0).toSun).shadowStrength > 0.9f);
}

TEST(default_light_levels_change_no_bit_of_the_sky) {
    // ABI 1.16: a host that never calls strata_set_ambient or
    // strata_set_sun_intensity must draw exactly what 1.15 drew.
    for (int i = 0; i < 200; ++i) {
        const SkyState original = evaluateSky(static_cast<float>(i) / 200.0f, 35.0f);
        SkyState adjusted = original;
        applyLightLevels(adjusted, LightLevels{});
        for (int c = 0; c < 3; ++c) {
            CHECK(adjusted.skyAmbient[c] == original.skyAmbient[c]);
            CHECK(adjusted.groundAmbient[c] == original.groundAmbient[c]);
            CHECK(adjusted.lightColor[c] == original.lightColor[c]);
        }
        CHECK(adjusted.shadowStrength == original.shadowStrength);
        CHECK(adjusted.zenith == original.zenith && adjusted.fogColor == original.fogColor);
    }
}

TEST(light_levels_tint_lift_and_scale) {
    const SkyState noon = evaluateSky(0.5f, 35.0f);
    LightLevels levels;
    levels.ambientTint = glm::vec3(2.0f, 1.0f, 0.5f);
    levels.ambientLift = glm::vec3(0.1f, 0.2f, 0.3f);
    levels.keyTint = glm::vec3(1.0f, 0.5f, 0.25f);
    levels.keyIntensity = 0.5f;
    SkyState s = noon;
    applyLightLevels(s, levels);
    for (int c = 0; c < 3; ++c) {
        CHECK_NEAR(s.skyAmbient[c], noon.skyAmbient[c] * levels.ambientTint[c] + levels.ambientLift[c], 1e-6);
        CHECK_NEAR(s.groundAmbient[c], noon.groundAmbient[c] * levels.ambientTint[c] + levels.ambientLift[c], 1e-6);
        CHECK_NEAR(s.lightColor[c], noon.lightColor[c] * levels.keyTint[c] * 0.5f, 1e-6);
    }
    // The sun keeps its direction, shadows and sky.
    CHECK(s.toLight == noon.toLight && s.shadowStrength == noon.shadowStrength && s.zenith == noon.zenith);
}
