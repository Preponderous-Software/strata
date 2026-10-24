#include "core/json_line.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace strata::json {
namespace {

struct Parser {
    explicit Parser(const std::string& text) : s(text) {}
    const std::string& s;
    size_t i = 0;
    std::string error;

    void skip() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    }
    bool fail(const char* what) {
        if (error.empty()) error = std::string(what) + " at byte " + std::to_string(i);
        return false;
    }
    bool literal(const char* word) {
        size_t n = 0;
        while (word[n]) ++n;
        if (s.compare(i, n, word) != 0) return fail("unexpected token");
        i += n;
        return true;
    }
    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& out) {
        if (i + 4 > s.size()) return fail("short \\u escape");
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }
    bool string(std::string& out) {
        if (i >= s.size() || s[i] != '"') return fail("expected a string");
        ++i;
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("control character in a string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size()) break;
            const char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00) {
                        unsigned low = 0;
                        if (i + 2 > s.size() || s[i] != '\\' || s[i + 1] != 'u') return fail("lone surrogate");
                        i += 2;
                        if (!hex4(low) || low < 0xDC00 || low >= 0xE000) return fail("bad surrogate pair");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool value(Value& v) {
        skip();
        if (i >= s.size()) return fail("expected a value");
        const char c = s[i];
        if (c == '"') {
            v.kind = Value::Kind::String;
            return string(v.text);
        }
        if (c == 't') { v.kind = Value::Kind::Bool; v.boolean = true; return literal("true"); }
        if (c == 'f') { v.kind = Value::Kind::Bool; v.boolean = false; return literal("false"); }
        if (c == 'n') { v.kind = Value::Kind::Null; return literal("null"); }
        if (c == '{' || c == '[') return fail("nested objects and arrays are not part of the protocol");
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char* begin = s.c_str() + i;
            char* end = nullptr;
            const double d = std::strtod(begin, &end);
            if (end == begin || !std::isfinite(d)) return fail("bad number");
            i += static_cast<size_t>(end - begin);
            v.kind = Value::Kind::Number;
            v.number = d;
            return true;
        }
        return fail("unexpected character");
    }
    bool object(Object& out) {
        skip();
        if (i >= s.size() || s[i] != '{') return fail("expected an object");
        ++i;
        skip();
        if (i < s.size() && s[i] == '}') {
            ++i;
        } else {
            for (;;) {
                skip();
                std::string k;
                if (!string(k)) return false;
                skip();
                if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                ++i;
                Value v;
                if (!value(v)) return false;
                out[k] = v;
                skip();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                return fail("expected ',' or '}'");
            }
        }
        skip();
        if (i != s.size()) return fail("trailing characters");
        return true;
    }
};

}  // namespace

bool parseObject(const std::string& line, Object& out, std::string* error) {
    out.clear();
    Parser p(line);
    if (p.object(out)) return true;
    if (error) *error = p.error;
    out.clear();
    return false;
}

std::string quote(const std::string& text) {
    std::string out = "\"";
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    return out + "\"";
}

void Writer::key(const char* k) {
    if (body_.size() > 1) body_ += ',';
    body_ += quote(k);
    body_ += ':';
}

Writer& Writer::field(const char* k, const std::string& value) {
    key(k);
    body_ += quote(value);
    return *this;
}

Writer& Writer::field(const char* k, const char* value) { return field(k, std::string(value ? value : "")); }

Writer& Writer::field(const char* k, double value) {
    key(k);
    if (!std::isfinite(value)) {
        body_ += "null";
    } else {
        char buf[40];
        std::snprintf(buf, sizeof buf, "%.17g", value);
        body_ += buf;
    }
    return *this;
}

Writer& Writer::field(const char* k, int value) { return field(k, static_cast<long long>(value)); }

Writer& Writer::field(const char* k, long long value) {
    key(k);
    body_ += std::to_string(value);
    return *this;
}

Writer& Writer::field(const char* k, bool value) {
    key(k);
    body_ += value ? "true" : "false";
    return *this;
}

}  // namespace strata::json
