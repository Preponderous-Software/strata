#include "adapters/stdio_console.hpp"

#include <cstdio>

namespace strata::golden {

void StdioConsole::out(const std::string& text) { std::fwrite(text.data(), 1, text.size(), stdout); }
void StdioConsole::err(const std::string& text) { std::fwrite(text.data(), 1, text.size(), stderr); }

}  // namespace strata::golden
