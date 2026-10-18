#include "core/log.hpp"

#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace strata {
namespace {

std::mutex g_sinkMutex;
StrataLogFn g_sink = nullptr;
void* g_sinkUser = nullptr;

const char* levelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warning: return "warning";
        case LogLevel::Error: return "error";
    }
    return "?";
}

}  // namespace

void setLogSink(StrataLogFn fn, void* user) {
    std::lock_guard<std::mutex> lock(g_sinkMutex);
    g_sink = fn;
    g_sinkUser = user;
}

void logf(LogLevel level, const char* format, ...) {
    char message[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof message, format, args);
    va_end(args);

    StrataLogFn sink;
    void* user;
    {
        std::lock_guard<std::mutex> lock(g_sinkMutex);
        sink = g_sink;
        user = g_sinkUser;
    }
    if (sink) {
        sink(static_cast<int32_t>(level), message, user);
    } else if (level != LogLevel::Debug || std::getenv("STRATA_LOG_DEBUG")) {
        std::fprintf(stderr, "strata %s: %s\n", levelName(level), message);
    }
}

}  // namespace strata
