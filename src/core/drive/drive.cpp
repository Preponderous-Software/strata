#include "core/drive/drive.hpp"

#include <cmath>
#include <cstring>

#include "core/drive/settings.hpp"
#include "core/error.hpp"
#include "core/json_line.hpp"
#include "core/log.hpp"

#ifndef STRATA_VERSION_STRING
#define STRATA_VERSION_STRING "0.0.0"
#endif

namespace strata::drive {
namespace {

struct TypeName {
    int32_t type;
    const char* name;
};

constexpr TypeName kTypeNames[] = {
    {STRATA_EVENT_QUIT, "quit"},
    {STRATA_EVENT_KEY_DOWN, "key_down"},
    {STRATA_EVENT_KEY_UP, "key_up"},
    {STRATA_EVENT_MOUSE_MOTION, "mouse_motion"},
    {STRATA_EVENT_MOUSE_BUTTON_DOWN, "mouse_button_down"},
    {STRATA_EVENT_MOUSE_BUTTON_UP, "mouse_button_up"},
    {STRATA_EVENT_MOUSE_WHEEL, "mouse_wheel"},
    {STRATA_EVENT_RESIZED, "resized"},
    {STRATA_EVENT_TEXT_INPUT, "text_input"},
};

const char* modeName(ClockMode mode) { return mode == ClockMode::Stepped ? "stepped" : "free"; }

const json::Value* find(const json::Object& o, const char* key) {
    const auto it = o.find(key);
    return it == o.end() ? nullptr : &it->second;
}

double number(const json::Object& o, const char* key, double fallback) {
    const json::Value* v = find(o, key);
    return v && v->kind == json::Value::Kind::Number ? v->number : fallback;
}

int32_t integer(const json::Object& o, const char* key) {
    const double d = number(o, key, 0.0);
    if (d > 2147483647.0 || d < -2147483648.0) return 0;
    return static_cast<int32_t>(d);
}

std::string text(const json::Object& o, const char* key) {
    const json::Value* v = find(o, key);
    return v && v->kind == json::Value::Kind::String ? v->text : std::string();
}

}  // namespace

int32_t eventTypeFromName(const std::string& name) {
    for (const TypeName& t : kTypeNames) {
        if (name == t.name) return t.type;
    }
    return STRATA_EVENT_NONE;
}

const char* eventTypeName(int32_t type) {
    for (const TypeName& t : kTypeNames) {
        if (t.type == type) return t.name;
    }
    return "none";
}

Drive::Drive(std::unique_ptr<MonotonicClock> clock) : clock_(std::move(clock)) {}

Drive::~Drive() {
    if (stepPending_ && connected()) {
        // The game is going away mid-step: answer so the harness is not left waiting.
        send(json::Writer()
                 .field("ok", true)
                 .field("op", "step")
                 .field("frame", static_cast<long long>(frame_))
                 .field("frames_run", static_cast<long long>(stepFramesRun_))
                 .field("ended", true)
                 .str());
    }
}

void Drive::attach(std::unique_ptr<HarnessLink> link, ClockMode mode, double step) {
    link_ = std::move(link);
    wasDriven_ = true;
    mode_ = mode;
    step_ = validStep(step) ? step : 1.0 / 60.0;
}

void Drive::push(const StrataEvent& event) {
    if (event.type <= STRATA_EVENT_NONE || event.type > STRATA_EVENT_TEXT_INPUT) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "event type %d is not a STRATA_EVENT_* value", event.type);
    }
    if (queue_.size() >= kMaxQueuedEvents) {
        fail(STRATA_ERROR_INVALID_STATE, "the injected event queue is full (%zu events)", kMaxQueuedEvents);
    }
    StrataEvent copy = event;
    copy.text[sizeof copy.text - 1] = '\0';
    if (relativeMouse_ && copy.type == STRATA_EVENT_MOUSE_MOTION) {
        copy.x = 0.0f;
        copy.y = 0.0f;
    }
    queue_.push_back(copy);
}

void Drive::beginPoll() { startFrame(); }

bool Drive::pop(StrataEvent& out) {
    if (queue_.empty()) return false;
    out = queue_.front();
    queue_.pop_front();
    return true;
}

void Drive::startFrame() {
    if (frameStarted_) return;
    frameStarted_ = true;
    if (!connected()) return;
    if (mode_ == ClockMode::Stepped) {
        awaitGrant();
        if (connected() && mode_ == ClockMode::Stepped && grantedFrames_ > 0) --grantedFrames_;
    } else {
        pump(false);
    }
}

double Drive::frameSeconds() {
    startFrame();
    const double now = clock_->now();
    if (connected() && mode_ == ClockMode::Stepped) {
        // Measured time restarts from here if the clock goes back to free.
        haveLast_ = false;
        return step_;
    }
    double seconds = 0.0;
    if (haveLast_) seconds = now - last_;
    last_ = now;
    haveLast_ = true;
    return seconds;
}

void Drive::presented() {
    ++frame_;
    frameStarted_ = false;
    if (stepPending_) {
        ++stepFramesRun_;
        if (grantedFrames_ == 0) {
            stepPending_ = false;
            send(json::Writer()
                     .field("ok", true)
                     .field("op", "step")
                     .field("frame", static_cast<long long>(frame_))
                     .field("frames_run", static_cast<long long>(stepFramesRun_))
                     .str());
        }
    }
}

void Drive::awaitGrant() {
    // Lines are taken one at a time and handling stops at the grant, so
    // anything the harness pipelined after a step waits for the next frame
    // boundary: what a step delivers never depends on how the bytes arrived.
    while (connected() && mode_ == ClockMode::Stepped && grantedFrames_ == 0) {
        const size_t newline = inbox_.find('\n');
        if (newline == std::string::npos) {
            readSome(true);
            continue;
        }
        const std::string line = inbox_.substr(0, newline);
        inbox_.erase(0, newline + 1);
        handleLine(line);
    }
}

void Drive::pump(bool block) {
    while (readSome(block)) block = false;
    while (connected() && mode_ == ClockMode::Free) {
        const size_t newline = inbox_.find('\n');
        if (newline == std::string::npos) break;
        const std::string line = inbox_.substr(0, newline);
        inbox_.erase(0, newline + 1);
        handleLine(line);
    }
}

bool Drive::readSome(bool block) {
    if (!link_) return false;
    Received got = link_->receive(block);
    if (got.kind == Received::Kind::Closed) {
        disconnect(got.reason.c_str());
        return false;
    }
    if (got.kind == Received::Kind::Nothing) return false;
    inbox_.append(got.bytes);
    if (inbox_.size() > kMaxLineBytes && inbox_.find('\n') == std::string::npos) {
        disconnect("a line longer than 1 MiB");
        return false;
    }
    return true;
}

void Drive::send(const std::string& line) {
    if (!link_) return;
    const Sent sent = link_->send(line + "\n");
    if (!sent.ok) disconnect(sent.reason.c_str());
}

void Drive::disconnect(const char* why) {
    if (!link_) return;
    // Copied before anything is reset, so `why` may point anywhere.
    const std::string reason = why;
    link_.reset();
    // A stepped game must never hang for a harness that has gone.
    mode_ = ClockMode::Free;
    grantedFrames_ = 0;
    stepPending_ = false;
    inbox_.clear();
    logf(LogLevel::Info, "drive channel closed (%s); running undriven", reason.c_str());
}

void Drive::handleLine(const std::string& raw) {
    std::string line = raw;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.find_first_not_of(" \t") == std::string::npos) return;
    json::Object msg;
    std::string error;
    if (!json::parseObject(line, msg, &error)) {
        send(json::Writer().field("ok", false).field("error", "bad JSON: " + error).str());
        return;
    }
    const std::string op = text(msg, "op");
    auto reply = [&] { return json::Writer().field("ok", true).field("op", op); };
    auto refuse = [&](const std::string& why) {
        send(json::Writer().field("ok", false).field("op", op).field("error", why).str());
    };

    if (op == "hello") {
        const int32_t protocol = static_cast<int32_t>(number(msg, "protocol", kProtocolVersion));
        if (protocol != kProtocolVersion) {
            refuse("this engine speaks drive protocol " + std::to_string(kProtocolVersion));
            return;
        }
        int32_t width = 0, height = 0;
        if (hooks_.drawableSize) hooks_.drawableSize(width, height);
        const std::string abi = std::to_string(STRATA_ABI_VERSION) + "." + std::to_string(STRATA_ABI_MINOR);
        send(reply()
                 .field("protocol", kProtocolVersion)
                 .field("strata", STRATA_VERSION_STRING)
                 .field("abi", abi)
                 .field("width", width)
                 .field("height", height)
                 .field("surface", hooks_.surfaceKind)
                 .field("clock", modeName(mode_))
                 .field("step", step_)
                 .field("frame", static_cast<long long>(frame_))
                 .str());
    } else if (op == "event") {
        StrataEvent e{};
        const json::Value* type = find(msg, "type");
        if (type && type->kind == json::Value::Kind::String) e.type = eventTypeFromName(type->text);
        else if (type && type->kind == json::Value::Kind::Number) e.type = static_cast<int32_t>(type->number);
        e.scancode = integer(msg, "scancode");
        e.modifiers = integer(msg, "modifiers");
        e.repeat = integer(msg, "repeat");
        e.button = integer(msg, "button");
        e.width = integer(msg, "width");
        e.height = integer(msg, "height");
        e.x = static_cast<float>(number(msg, "x", 0.0));
        e.y = static_cast<float>(number(msg, "y", 0.0));
        e.dx = static_cast<float>(number(msg, "dx", 0.0));
        e.dy = static_cast<float>(number(msg, "dy", 0.0));
        const std::string t = text(msg, "text");
        if (t.size() >= sizeof e.text) {
            refuse("text is longer than 31 bytes; send it as several text_input events");
            return;
        }
        std::memcpy(e.text, t.c_str(), t.size() + 1);
        try {
            push(e);
        } catch (const Error& err) {
            refuse(err.what());
            return;
        }
        send(reply().field("queued", static_cast<long long>(queue_.size())).str());
    } else if (op == "clock") {
        const std::string mode = text(msg, "mode");
        const double step = number(msg, "step", step_);
        if (mode != "free" && mode != "stepped") {
            refuse("mode must be free or stepped");
            return;
        }
        if (!validStep(step)) {
            refuse("step must be in (0, 1] seconds");
            return;
        }
        mode_ = mode == "stepped" ? ClockMode::Stepped : ClockMode::Free;
        step_ = step;
        send(reply().field("clock", modeName(mode_)).field("step", step_).str());
    } else if (op == "step") {
        const double frames = number(msg, "frames", 1.0);
        if (mode_ != ClockMode::Stepped) {
            refuse("step needs the stepped clock (send clock first)");
            return;
        }
        if (!(frames >= 1.0) || frames > static_cast<double>(kMaxStepFrames) || frames != std::floor(frames)) {
            refuse("frames must be a whole number from 1 to 1000000");
            return;
        }
        grantedFrames_ = static_cast<int64_t>(frames);
        stepPending_ = true;
        stepFramesRun_ = 0;
    } else if (op == "shot") {
        const std::string path = text(msg, "path");
        if (path.empty()) {
            refuse("shot needs a path");
            return;
        }
        if (!hooks_.screenshot) {
            refuse("this engine cannot take screenshots");
            return;
        }
        try {
            hooks_.screenshot(path);
        } catch (const std::exception& err) {
            refuse(err.what());
            return;
        }
        int32_t width = 0, height = 0;
        if (hooks_.drawableSize) hooks_.drawableSize(width, height);
        send(reply().field("path", path).field("width", width).field("height", height).str());
    } else if (op == "stats") {
        StrataFrameStats s{};
        if (hooks_.frameStats) s = hooks_.frameStats();
        send(reply()
                 .field("frame", static_cast<long long>(frame_))
                 .field("clock", modeName(mode_))
                 .field("step", step_)
                 .field("queued", static_cast<long long>(queue_.size()))
                 .field("draw_calls", s.draw_calls)
                 .field("instances", s.instances)
                 .field("cpu_ms_frame", static_cast<double>(s.cpu_ms_frame))
                 .str());
    } else if (op == "bye") {
        send(reply().str());
        disconnect("the harness said bye");
    } else {
        refuse(op.empty() ? std::string("missing op") : "unknown op '" + op + "'");
    }
}

}  // namespace strata::drive
