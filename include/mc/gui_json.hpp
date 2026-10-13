#pragma once
#include "mc/protocol.hpp"
#include <map>
#include <string>
#include <vector>
namespace mc::gui_json {
using net::ProtocolError;
struct Json {
    enum class Kind { Null, String, Number, Boolean, Array, Object } kind{Kind::Null};
    std::string value;
    std::vector<Json> array;
    std::map<std::string, Json> object;
    const Json* get(const std::string& name) const {
        const auto found = object.find(name);
        return found == object.end() ? nullptr : &found->second;
    }
};
inline void appendUtf8(std::string& result, unsigned cp) {
    if (cp < 0x80) result.push_back(static_cast<char>(cp));
    else if (cp < 0x800) { result.push_back(static_cast<char>(0xc0 | (cp >> 6))); result.push_back(static_cast<char>(0x80 | (cp & 63))); }
    else if (cp < 0x10000) {
        result.push_back(static_cast<char>(0xe0 | (cp >> 12))); result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
        result.push_back(static_cast<char>(0x80 | (cp & 63)));
    } else {
        result.push_back(static_cast<char>(0xf0 | (cp >> 18))); result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 63)));
        result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63))); result.push_back(static_cast<char>(0x80 | (cp & 63)));
    }
}
class JsonParser {
public:
    explicit JsonParser(const std::string& text,std::size_t byteLimit=131068,std::size_t nodeLimit=32768) : text_(text),byteLimit_(byteLimit),nodeLimit_(nodeLimit) {}
    Json parse() {
        if (text_.size() > byteLimit_) throw ProtocolError("JSON length limit");
        auto result = value(0); space();
        if (pos_ != text_.size()) throw ProtocolError("Trailing chat JSON");
        return result;
    }
private:
    const std::string& text_;
    std::size_t byteLimit_,nodeLimit_;
    std::size_t pos_{}, nodes_{};
    void space() { while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\r' || text_[pos_] == '\n')) ++pos_; }
    char take() { if (pos_ == text_.size()) throw ProtocolError("Truncated chat JSON"); return text_[pos_++]; }
    unsigned hex4() {
        unsigned result = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = take();
            result <<= 4;
            if (c >= '0' && c <= '9') result += c - '0';
            else if (c >= 'a' && c <= 'f') result += c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') result += c - 'A' + 10;
            else throw ProtocolError("Invalid JSON Unicode escape");
        }
        return result;
    }
    std::string string() {
        if (take() != '"') throw ProtocolError("Expected JSON string");
        std::string out;
        for (;;) {
            const auto c = static_cast<unsigned char>(take());
            if (c == '"') return out;
            if (c < 32) throw ProtocolError("Control character in JSON string");
            if (c != '\\') { out.push_back(static_cast<char>(c)); continue; }
            switch (take()) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = hex4();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (take() != '\\' || take() != 'u') throw ProtocolError("Unpaired JSON surrogate");
                    const auto low = hex4();
                    if (low < 0xdc00 || low > 0xdfff) throw ProtocolError("Invalid JSON low surrogate");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                } else if (cp >= 0xdc00 && cp <= 0xdfff) throw ProtocolError("Unpaired JSON low surrogate");
                appendUtf8(out, cp); break;
            }
            default: throw ProtocolError("Invalid JSON string escape");
            }
        }
    }
    Json value(unsigned depth) {
        if (depth > 64 || ++nodes_ > nodeLimit_) throw ProtocolError("JSON nesting/node limit");
        space();
        if (pos_ == text_.size()) throw ProtocolError("Empty JSON value");
        Json out;
        const char first = text_[pos_];
        if (first == '"') { out.kind = Json::Kind::String; out.value = string(); return out; }
        if (first == '[') {
            out.kind = Json::Kind::Array; ++pos_; space();
            if (pos_ < text_.size() && text_[pos_] == ']') { ++pos_; return out; }
            for (;;) {
                out.array.push_back(value(depth + 1)); space();
                const char separator = take();
                if (separator == ']') return out;
                if (separator != ',') throw ProtocolError("Invalid JSON array separator");
            }
        }
        if (first == '{') {
            out.kind = Json::Kind::Object; ++pos_; space();
            if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; return out; }
            for (;;) {
                space(); const auto key = string(); space();
                if (take() != ':') throw ProtocolError("Invalid JSON member separator");
                out.object[key] = value(depth + 1); space();
                const char separator = take();
                if (separator == '}') return out;
                if (separator != ',') throw ProtocolError("Invalid JSON object separator");
            }
        }
        for (const std::string literal : {"null", "true", "false"}) {
            if (text_.compare(pos_, literal.size(), literal) == 0) {
                pos_ += literal.size(); out.kind = literal == "null" ? Json::Kind::Null : Json::Kind::Boolean;
                out.value = literal; return out;
            }
        }
        out.kind = Json::Kind::Number;
        const auto start = pos_;
        if (text_[pos_] == '-') ++pos_;
        if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') throw ProtocolError("Invalid JSON number");
        if (text_[pos_] == '0') ++pos_;
        else while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_; const auto digits = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            if (pos_ == digits) throw ProtocolError("Invalid JSON fraction");
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_; if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            const auto digits = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            if (pos_ == digits) throw ProtocolError("Invalid JSON exponent");
        }
        out.value = text_.substr(start, pos_ - start);
        return out;
    }
};


} // namespace mc::gui_json
