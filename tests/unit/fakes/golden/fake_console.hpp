#pragma once

#include <string>

#include "ports/console.hpp"

namespace strata::golden {

// Console that keeps everything: `text` interleaves both streams, err lines
// marked with "!".
class FakeConsole final : public Console {
public:
    std::string text;
    void out(const std::string& t) override { text += t; }
    void err(const std::string& t) override { text += "!" + t; }
};

}  // namespace strata::golden
