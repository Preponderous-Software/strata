// Parsing the drive channel's environment variables, as a pure function: the
// composition root reads the variables and logs the warnings.
#pragma once

#include "core/drive/model.hpp"

namespace strata::drive {

// True for a step a harness may ask for: finite, in (0, 1] seconds.
bool validStep(double seconds);

// `path`, `clock` and `step` are the values of STRATA_DRIVE,
// STRATA_DRIVE_CLOCK and STRATA_DRIVE_STEP, or null when unset. Without a
// path nothing else is read and there are no warnings.
DriveSettings parseDriveSettings(const char* path, const char* clock, const char* step);

}  // namespace strata::drive
