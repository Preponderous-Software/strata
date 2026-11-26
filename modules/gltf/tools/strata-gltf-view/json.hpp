// A small JSON reader for strata-gltf-view's camera files: objects, arrays,
// numbers, strings (with escapes; \u kept as '?'), true, false and null.
#pragma once

#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace gltfview {

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Json> array;
    std::map<std::string, Json> object;

    const Json* find(const std::string& key) const {
        if (type != Type::Object) return nullptr;
        auto it = object.find(key);
        return it == object.end() ? nullptr : &it->second;
    }
};

class JsonReader {
public:
    explicit JsonReader(const std::string& text) : s_(text) {}

    Json parse() {
        Json v = value();
        space();
        if (i_ != s_.size()) fail("trailing characters");
        return v;
    }

private:
    [[noreturn]] void fail(const char* what) const {
        throw std::runtime_error(std::string("JSON: ") + what + " at offset " + std::to_string(i_));
    }

    void space() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }

    bool literal(const char* word) {
        const std::string w(word);
        if (s_.compare(i_, w.size(), w) != 0) return false;
        i_ += w.size();
        return true;
    }

    std::string str() {
        if (s_[i_] != '"') fail("expected a string");
        ++i_;
        std::string out;
        while (i_ < s_.size() && s_[i_] != '"') {
            char c = s_[i_++];
            if (c == '\\') {
                if (i_ >= s_.size()) fail("bad escape");
                const char e = s_[i_++];
                switch (e) {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'u':
                        if (i_ + 4 > s_.size()) fail("bad \\u escape");
                        i_ += 4;
                        c = '?';
                        break;
                    default: c = e; break;
                }
            }
            out += c;
        }
        if (i_ >= s_.size()) fail("unterminated string");
        ++i_;
        return out;
    }

    Json value() {
        if (++depth_ > 64) fail("nested too deeply");
        space();
        if (i_ >= s_.size()) fail("unexpected end");
        Json v;
        const char c = s_[i_];
        if (c == '{') {
            v.type = Json::Type::Object;
            ++i_;
            space();
            if (i_ < s_.size() && s_[i_] == '}') {
                ++i_;
            } else {
                for (;;) {
                    space();
                    std::string key = str();
                    space();
                    if (i_ >= s_.size() || s_[i_] != ':') fail("expected ':'");
                    ++i_;
                    v.object[key] = value();
                    space();
                    if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                    if (i_ < s_.size() && s_[i_] == '}') { ++i_; break; }
                    fail("expected ',' or '}'");
                }
            }
        } else if (c == '[') {
            v.type = Json::Type::Array;
            ++i_;
            space();
            if (i_ < s_.size() && s_[i_] == ']') {
                ++i_;
            } else {
                for (;;) {
                    v.array.push_back(value());
                    space();
                    if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                    if (i_ < s_.size() && s_[i_] == ']') { ++i_; break; }
                    fail("expected ',' or ']'");
                }
            }
        } else if (c == '"') {
            v.type = Json::Type::String;
            v.string = str();
        } else if (literal("true")) {
            v.type = Json::Type::Bool;
            v.boolean = true;
        } else if (literal("false")) {
            v.type = Json::Type::Bool;
        } else if (literal("null")) {
            v.type = Json::Type::Null;
        } else {
            const char* begin = s_.c_str() + i_;
            char* end = nullptr;
            v.number = std::strtod(begin, &end);
            if (end == begin) fail("unexpected character");
            v.type = Json::Type::Number;
            i_ += static_cast<size_t>(end - begin);
        }
        --depth_;
        return v;
    }

    const std::string& s_;
    size_t i_ = 0;
    int depth_ = 0;
};

}  // namespace gltfview
