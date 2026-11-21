#include "core/drive/service.hpp"

#include <cstdlib>

#include "core/drive/adapters/steady_clock.hpp"
#include "core/drive/adapters/unix_socket_link.hpp"
#include "core/drive/settings.hpp"
#include "core/log.hpp"

namespace strata::drive {

std::unique_ptr<Drive> createDrive() { return std::make_unique<Drive>(std::make_unique<SteadyClock>()); }

void connectFromEnvironment(Drive& drive) {
    const DriveSettings settings = parseDriveSettings(std::getenv("STRATA_DRIVE"), std::getenv("STRATA_DRIVE_CLOCK"),
                                                      std::getenv("STRATA_DRIVE_STEP"));
    if (!settings.enabled) return;
    for (const std::string& warning : settings.warnings) logf(LogLevel::Warning, "%s", warning.c_str());
    std::string why;
    std::unique_ptr<UnixSocketLink> link = UnixSocketLink::connect(settings.path, why);
    if (!link) {
        logf(LogLevel::Warning, "%s", why.c_str());
        return;
    }
    drive.attach(std::move(link), settings.mode, settings.step);
    logf(LogLevel::Info, "driven through %s (%s clock, step %g s)", settings.path.c_str(),
         drive.clockMode() == ClockMode::Stepped ? "stepped" : "free", drive.step());
}

}  // namespace strata::drive
