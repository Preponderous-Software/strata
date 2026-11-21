#include "core/drive/settings.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace strata::drive {

bool validStep(double seconds) { return std::isfinite(seconds) && seconds > 0.0 && seconds <= 1.0; }

DriveSettings parseDriveSettings(const char* path, const char* clock, const char* s) {
    DriveSettings out;
    if (!path || !*path) return out;
    out.enabled = true;
    out.path = path;
    if (clock) {
        if (std::strcmp(clock, "stepped") == 0) {
            out.mode = ClockMode::Stepped;
        } else if (*clock && std::strcmp(clock, "free") != 0) {
            out.warnings.push_back(std::string("STRATA_DRIVE_CLOCK must be free or stepped (got '") + clock +
                                   "'); using free");
        }
    }
    if (s) {
        const double parsed = std::strtod(s, nullptr);
        if (validStep(parsed)) {
            out.step = parsed;
        } else if (*s) {
            out.warnings.push_back(std::string("STRATA_DRIVE_STEP must be in (0, 1] seconds (got '") + s +
                                   "'); using 1/60");
        }
    }
    return out;
}

}  // namespace strata::drive
