#pragma once

#include <string>

#include "ports/console.hpp"

namespace strata::golden {

// Console on the C streams: out() to stdout, err() to stderr.
class StdioConsole final : public Console {
public:
    void out(const std::string& text) override;
    void err(const std::string& text) override;
};

}  // namespace strata::golden
