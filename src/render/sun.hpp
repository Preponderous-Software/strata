// The sun, the moon and the sky they light, as a function of time of day.
//
// The game owns time: it passes time_of_day in [0,1) and the engine only
// evaluates it. World axes: +Y up, +X east, +Z south (so -Z is north).
// The sun follows an equinox path (declination 0) tilted by latitude, rising
// due east at 0.25 and setting due west at 0.75.
#pragma once

#include <glm/glm.hpp>

namespace strata::render {

struct SkyState {
    glm::vec3 toSun{0, 1, 0};     // unit vector towards the sun
    float sunElevation = 1.0f;    // sine of the sun's elevation
    glm::vec3 toLight{0, 1, 0};   // the active key light: the sun by day, the moon by night
    glm::vec3 lightColor{1.0f};   // key light radiance (0 when neither is up)
    float shadowStrength = 1.0f;  // 0..1; 0 means the key light casts no shadow at all
    bool lightIsMoon = false;
    glm::vec3 skyAmbient{0.3f};     // hemisphere ambient from above
    glm::vec3 groundAmbient{0.2f};  // and from below
    glm::vec3 zenith{0.3f, 0.5f, 0.8f};
    glm::vec3 horizon{0.7f, 0.8f, 0.9f};
    glm::vec3 fogColor{0.7f, 0.8f, 0.9f};
};

SkyState evaluateSky(float timeOfDay, float latitudeDegrees);
// The same sky for a sun in a given direction (unit vector towards it), for
// hosts that place the sun themselves (ABI 1.6).
SkyState evaluateSkyForSun(const glm::vec3& toSun);

// ABI 1.16. The host's adjustments on top of the evaluated sky
// (strata_set_ambient, strata_set_sun_intensity). The defaults change nothing:
// x * 1 + 0 is x exactly, so a host that never calls them draws what it drew.
struct LightLevels {
    glm::vec3 ambientTint{1.0f};  // multiplies the sky and ground ambient
    glm::vec3 ambientLift{0.0f};  // then added to both
    glm::vec3 keyTint{1.0f};      // multiplies the key light (sun, or moon at night)
    float keyIntensity = 1.0f;    // and so does this
};
void applyLightLevels(SkyState& sky, const LightLevels& levels);

// Unit vector towards the sun, before any colour is decided.
glm::vec3 sunDirection(float timeOfDay, float latitudeDegrees);

// ABI 1.6. The sun's position for a moment (seconds since 1970-01-01 UTC) and
// a place on Earth, from NOAA's solar position equations (after Meeus): good
// to about a minute of arc between 1800 and 2100. Geometric: no atmospheric
// refraction. Arguments are not validated here.
struct SolarPosition {
    glm::vec3 toSun{0, 1, 0};  // unit, world frame (+X east, +Y up, +Z south)
    double elevationDegrees = 90.0;
    double azimuthDegrees = 180.0;  // clockwise from north
    double declinationDegrees = 0.0;
    double hourAngleDegrees = 0.0;  // 0 at local solar noon, negative before it
    double equationOfTimeMinutes = 0.0;
};
SolarPosition solarPosition(double unixSecondsUtc, double latitudeDegrees, double longitudeDegrees);

}  // namespace strata::render
