// One flat JSON object per line: the drive channel's wire format (docs/DRIVE.md).
//
// Only what the protocol needs: an object whose values are strings, numbers,
// booleans or null. Nested objects and arrays are rejected. No GL, no I/O, so
// it is unit-tested on its own.
#pragma once

#include <map>
#include <string>

namespace strata::json {

struct Value {
    enum class Kind { Null, Bool, Number, String };
    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
};

using Object = std::map<std::string, Value>;

// Parses one line holding a single flat object. On failure returns false and
// sets *error.
bool parseObject(const std::string& line, Object& out, std::string* error);

// Builds one line (without the trailing newline). Keys keep insertion order.
class Writer {
public:
    Writer& field(const char* key, const std::string& value);
    Writer& field(const char* key, const char* value);
    Writer& field(const char* key, double value);
    Writer& field(const char* key, int value);
    Writer& field(const char* key, long long value);
    Writer& field(const char* key, bool value);
    std::string str() const { return body_ + "}"; }

private:
    void key(const char* k);
    std::string body_ = "{";
};

std::string quote(const std::string& text);

}  // namespace strata::json
