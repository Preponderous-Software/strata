// The frame is a fixed, ordered list of passes, not a general frame graph.
//
// Each pass declares the resources it reads and writes. Nothing schedules
// from these declarations; they exist so the order can be validated (every
// read has an earlier writer or is a frame input) and so debug tooling can
// print the graph.
#pragma once

#include <string>
#include <vector>

namespace strata::render {

struct PassInfo {
    const char* name;
    std::vector<const char*> reads;
    std::vector<const char*> writes;
};

// The engine's pass list, in execution order.
const std::vector<PassInfo>& framePasses();

// Resources supplied from outside the pass list each frame.
const std::vector<const char*>& frameInputs();

// Empty when the order is valid; otherwise one line per problem.
std::vector<std::string> validatePassOrder(const std::vector<PassInfo>& passes,
                                           const std::vector<const char*>& inputs);

// "shadow: instances -> shadow_map" lines, for logs and docs.
std::string describePasses(const std::vector<PassInfo>& passes);

}  // namespace strata::render
