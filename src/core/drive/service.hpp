// The drive channel's composition root: builds a Drive with the production
// adapters (SteadyClock, UnixSocketLink) and connects it from the environment.
#pragma once

#include <memory>

#include "core/drive/drive.hpp"

namespace strata::drive {

// An undriven Drive on the steady clock.
std::unique_ptr<Drive> createDrive();

// Reads STRATA_DRIVE (a Unix socket path), STRATA_DRIVE_CLOCK (free|stepped)
// and STRATA_DRIVE_STEP (seconds) and connects. Without STRATA_DRIVE nothing
// happens; a failed connection logs a warning and leaves the engine undriven.
// Never throws.
void connectFromEnvironment(Drive& drive);

}  // namespace strata::drive
