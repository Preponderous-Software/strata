#pragma once

#include <string>

namespace strata::golden {

// Where the report goes. out() is the report (stdout), err() the problems
// (stderr). Text is written as given, newlines included. Never throws;
// single-threaded.
class Console {
public:
    virtual ~Console() = default;
    virtual void out(const std::string& text) = 0;
    virtual void err(const std::string& text) = 0;
};

}  // namespace strata::golden
