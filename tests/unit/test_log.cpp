// The log sink: strata_set_log_callback routes every engine message, with its
// level and the host's user pointer, to the host; NULL restores stderr.
#include <string>
#include <vector>

#include "core/log.hpp"
#include "strata/strata.h"
#include "test.hpp"

namespace {

struct Captured {
    std::vector<int32_t> levels;
    std::vector<std::string> messages;
};

void capture(int32_t level, const char* message, void* user) {
    auto* out = static_cast<Captured*>(user);
    out->levels.push_back(level);
    out->messages.emplace_back(message);
}

}  // namespace

TEST(log_callback_receives_level_message_and_user_pointer) {
    Captured captured;
    strata_set_log_callback(capture, &captured);
    strata::logf(strata::LogLevel::Debug, "pass %d", 1);
    strata::logf(strata::LogLevel::Info, "surface %s", "headless");
    strata::logf(strata::LogLevel::Warning, "warn");
    strata::logf(strata::LogLevel::Error, "GL: %s", "bad");
    strata_set_log_callback(nullptr, nullptr);

    REQUIRE(captured.levels.size() == 4);
    CHECK(captured.levels[0] == 0);
    CHECK(captured.levels[1] == 1);
    CHECK(captured.levels[2] == 2);
    CHECK(captured.levels[3] == 3);
    CHECK(captured.messages[0] == "pass 1");
    CHECK(captured.messages[1] == "surface headless");
    CHECK(captured.messages[3] == "GL: bad");
}

TEST(null_log_callback_stops_delivery) {
    Captured captured;
    strata_set_log_callback(capture, &captured);
    strata_set_log_callback(nullptr, nullptr);
    strata::logf(strata::LogLevel::Debug, "not delivered");
    CHECK(captured.messages.empty());
}

TEST(long_log_messages_are_truncated_not_overrun) {
    Captured captured;
    strata_set_log_callback(capture, &captured);
    const std::string longText(4000, 'x');
    strata::logf(strata::LogLevel::Info, "%s", longText.c_str());
    strata_set_log_callback(nullptr, nullptr);

    REQUIRE(captured.messages.size() == 1);
    CHECK(captured.messages[0].size() == 1023);
    CHECK(captured.messages[0] == std::string(1023, 'x'));
}
