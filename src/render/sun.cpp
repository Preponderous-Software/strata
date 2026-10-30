#include "render/sun.hpp"

#include <cmath>

namespace strata::render {
namespace {

constexpr float kPi = 3.14159265358979f;

float smoothstep(float edge0, float edge1, float x) {
    const float t = glm::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace

glm::vec3 sunDirection(float timeOfDay, float latitudeDegrees) {
    const float t = timeOfDay - std::floor(timeOfDay);
    const float hourAngle = (t - 0.5f) * 2.0f * kPi;  // 0 at solar noon
    const float latitude = glm::radians(latitudeDegrees);
    // Declination 0: east = -sin(h), up = cos(lat) cos(h), north = -sin(lat) cos(h).
    const float east = -std::sin(hourAngle);
    const float up = std::cos(latitude) * std::cos(hourAngle);
    const float north = -std::sin(latitude) * std::cos(hourAngle);
    return glm::normalize(glm::vec3(east, up, -north));
}

SolarPosition solarPosition(double unixSecondsUtc, double latitudeDegrees, double longitudeDegrees) {
    constexpr double kDeg = 3.14159265358979323846 / 180.0;
    auto wrap360 = [](double a) {
        a = std::fmod(a, 360.0);
        return a < 0.0 ? a + 360.0 : a;
    };
    const double julianDay = unixSecondsUtc / 86400.0 + 2440587.5;
    const double jc = (julianDay - 2451545.0) / 36525.0;  // Julian centuries since J2000.0

    const double meanLong = wrap360(280.46646 + jc * (36000.76983 + jc * 0.0003032));
    const double meanAnom = 357.52911 + jc * (35999.05029 - 0.0001537 * jc);
    const double eccent = 0.016708634 - jc * (0.000042037 + 0.0000001267 * jc);
    const double m = meanAnom * kDeg;
    const double centre = std::sin(m) * (1.914602 - jc * (0.004817 + 0.000014 * jc)) +
                          std::sin(2.0 * m) * (0.019993 - 0.000101 * jc) + std::sin(3.0 * m) * 0.000289;
    const double trueLong = meanLong + centre;
    const double omega = (125.04 - 1934.136 * jc) * kDeg;
    const double apparentLong = trueLong - 0.00569 - 0.00478 * std::sin(omega);
    const double meanObliquity =
        23.0 + (26.0 + (21.448 - jc * (46.815 + jc * (0.00059 - jc * 0.001813))) / 60.0) / 60.0;
    const double obliquity = (meanObliquity + 0.00256 * std::cos(omega)) * kDeg;
    const double declination = std::asin(std::sin(obliquity) * std::sin(apparentLong * kDeg));

    const double y = std::tan(obliquity / 2.0) * std::tan(obliquity / 2.0);
    const double l0 = meanLong * kDeg;
    const double eotRadians = y * std::sin(2.0 * l0) - 2.0 * eccent * std::sin(m) +
                              4.0 * eccent * y * std::sin(m) * std::cos(2.0 * l0) -
                              0.5 * y * y * std::sin(4.0 * l0) - 1.25 * eccent * eccent * std::sin(2.0 * m);
    const double eotMinutes = 4.0 * eotRadians / kDeg;

    double dayMinutes = std::fmod(unixSecondsUtc / 60.0, 1440.0);
    if (dayMinutes < 0.0) dayMinutes += 1440.0;
    const double trueSolarMinutes = dayMinutes + eotMinutes + 4.0 * longitudeDegrees;
    double hourAngle = wrap360(trueSolarMinutes / 4.0) - 180.0;  // degrees, in [-180, 180)

    const double lat = latitudeDegrees * kDeg, h = hourAngle * kDeg;
    const double east = -std::cos(declination) * std::sin(h);
    const double north = std::cos(lat) * std::sin(declination) - std::sin(lat) * std::cos(declination) * std::cos(h);
    const double up = std::sin(lat) * std::sin(declination) + std::cos(lat) * std::cos(declination) * std::cos(h);

    SolarPosition p;
    p.toSun = glm::normalize(glm::vec3(static_cast<float>(east), static_cast<float>(up), static_cast<float>(-north)));
    p.elevationDegrees = std::asin(glm::clamp(up, -1.0, 1.0)) / kDeg;
    p.azimuthDegrees = wrap360(std::atan2(east, north) / kDeg);
    p.declinationDegrees = declination / kDeg;
    p.hourAngleDegrees = hourAngle;
    p.equationOfTimeMinutes = eotMinutes;
    return p;
}

SkyState evaluateSky(float timeOfDay, float latitudeDegrees) {
    return evaluateSkyForSun(sunDirection(timeOfDay, latitudeDegrees));
}

SkyState evaluateSkyForSun(const glm::vec3& toSun) {
    SkyState s;
    s.toSun = toSun;
    const float e = s.toSun.y;
    s.sunElevation = e;

    // 1 at full day, 0 at full night, with a twilight band around the horizon.
    const float day = smoothstep(-0.12f, 0.25f, e);
    // Peaks while the sun is near the horizon on either side of it.
    const float twilight = 1.0f - smoothstep(0.0f, 0.22f, std::fabs(e - 0.02f));

    const glm::vec3 dayZenith(0.22f, 0.42f, 0.78f), dayHorizon(0.66f, 0.78f, 0.92f);
    const glm::vec3 nightZenith(0.010f, 0.014f, 0.040f), nightHorizon(0.040f, 0.050f, 0.090f);
    const glm::vec3 duskHorizon(0.93f, 0.52f, 0.30f);
    s.zenith = glm::mix(nightZenith, dayZenith, day);
    s.horizon = glm::mix(glm::mix(nightHorizon, dayHorizon, day), duskHorizon, twilight * 0.7f);
    s.fogColor = s.horizon;

    s.skyAmbient = glm::mix(glm::vec3(0.045f, 0.055f, 0.095f), glm::vec3(0.34f, 0.40f, 0.50f), day);
    s.groundAmbient = glm::mix(glm::vec3(0.020f, 0.020f, 0.030f), glm::vec3(0.20f, 0.18f, 0.15f), day);
    s.skyAmbient = glm::mix(s.skyAmbient, glm::vec3(0.40f, 0.30f, 0.30f), twilight * 0.25f);

    if (e > -0.02f) {
        // The sun: warm when low, white when high, fading in through sunrise.
        const glm::vec3 low(1.00f, 0.50f, 0.25f), high(1.00f, 0.96f, 0.90f);
        const float intensity = 1.1f * smoothstep(-0.02f, 0.10f, e);
        s.toLight = s.toSun;
        s.lightColor = glm::mix(low, high, smoothstep(0.0f, 0.45f, e)) * intensity;
        // No shadows from a sun at the horizon: they would stretch across the
        // whole world and flicker as the sun moves.
        s.shadowStrength = smoothstep(0.03f, 0.12f, e);
        s.lightIsMoon = false;
    } else {
        // The moon: opposite the sun, dim, bluish and shadowless.
        s.toLight = glm::normalize(glm::vec3(-s.toSun.x, std::fabs(s.toSun.y) + 0.25f, -s.toSun.z));
        s.lightColor = glm::vec3(0.10f, 0.12f, 0.20f) * smoothstep(-0.02f, -0.15f, e);
        s.shadowStrength = 0.0f;
        s.lightIsMoon = true;
    }
    return s;
}

void applyLightLevels(SkyState& sky, const LightLevels& levels) {
    sky.skyAmbient = sky.skyAmbient * levels.ambientTint + levels.ambientLift;
    sky.groundAmbient = sky.groundAmbient * levels.ambientTint + levels.ambientLift;
    sky.lightColor = sky.lightColor * (levels.keyTint * levels.keyIntensity);
}

}  // namespace strata::render
