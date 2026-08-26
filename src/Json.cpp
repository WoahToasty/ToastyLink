#include "ToastyLink/Json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace tl {

JsonValue JsonValue::MakeObject() { JsonValue v; v.kind = Kind::Object; return v; }
JsonValue JsonValue::MakeArray() { JsonValue v; v.kind = Kind::Array; return v; }
JsonValue JsonValue::MakeString(const std::string& s) { JsonValue v; v.kind = Kind::String; v.strVal = s; return v; }
JsonValue JsonValue::MakeNumber(double n) { JsonValue v; v.kind = Kind::Number; v.numVal = n; return v; }
JsonValue JsonValue::MakeBool(bool b) { JsonValue v; v.kind = Kind::Bool; v.boolVal = b; return v; }

void JsonValue::Set(const std::string& key, JsonValue v) {
    for (auto& kv : objVal) {
        if (kv.first == key) {
            kv.second = std::move(v);
            return;
        }
    }
    objVal.emplace_back(key, std::move(v));
}

const JsonValue* JsonValue::Find(const std::string& key) const {
    for (auto& kv : objVal) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

std::optional<std::string> JsonValue::AsString() const {
    if (kind != Kind::String) return std::nullopt;
    return strVal;
}

std::optional<double> JsonValue::AsNumber() const {
    if (kind != Kind::Number) return std::nullopt;
    return numVal;
}

std::optional<bool> JsonValue::AsBool() const {
    if (kind != Kind::Bool) return std::nullopt;
    return boolVal;
}

namespace {

void EscapeInto(std::ostringstream& out, const std::string& s) {
    out << '"';
    for (char c : s) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out << buf;
                } else {
                    out << c;
                }
        }
    }
    out << '"';
}

std::string NumberToString(double n) {
    if (n == static_cast<long long>(n) && std::abs(n) < 1e15) {
        std::ostringstream oss;
        oss << static_cast<long long>(n);
        return oss.str();
    }
    std::ostringstream oss;
    oss.precision(15);
    oss << n;
    return oss.str();
}

} // namespace

std::string JsonValue::Dump(int indent) const {
    std::string pad(static_cast<size_t>(indent) * 2, ' ');
    std::string padIn(static_cast<size_t>(indent + 1) * 2, ' ');
    std::ostringstream out;
    switch (kind) {
        case Kind::Null: out << "null"; break;
        case Kind::Bool: out << (boolVal ? "true" : "false"); break;
        case Kind::Number: out << NumberToString(numVal); break;
        case Kind::String: EscapeInto(out, strVal); break;
        case Kind::Array: {
            if (arrVal.empty()) { out << "[]"; break; }
            out << "[\n";
            for (size_t i = 0; i < arrVal.size(); ++i) {
                out << padIn << arrVal[i].Dump(indent + 1);
                if (i + 1 < arrVal.size()) out << ",";
                out << "\n";
            }
            out << pad << "]";
            break;
        }
        case Kind::Object: {
            if (objVal.empty()) { out << "{}"; break; }
            out << "{\n";
            for (size_t i = 0; i < objVal.size(); ++i) {
                out << padIn;
                EscapeInto(out, objVal[i].first);
                out << ": " << objVal[i].second.Dump(indent + 1);
                if (i + 1 < objVal.size()) out << ",";
                out << "\n";
            }
            out << pad << "}";
            break;
        }
    }
    return out.str();
}

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : m_text(text) {}

    std::optional<JsonValue> Parse(std::string* err) {
        SkipWs();
        auto v = ParseValue(err);
        if (!v) return std::nullopt;
        SkipWs();
        if (m_pos != m_text.size()) {
            if (err) *err = "trailing data after JSON value";
            return std::nullopt;
        }
        return v;
    }

private:
    const std::string& m_text;
    size_t m_pos = 0;

    bool AtEnd() const { return m_pos >= m_text.size(); }
    char Peek() const { return m_text[m_pos]; }

    void SkipWs() {
        while (!AtEnd() && std::isspace(static_cast<unsigned char>(Peek()))) ++m_pos;
    }

    bool Consume(char c) {
        if (!AtEnd() && Peek() == c) { ++m_pos; return true; }
        return false;
    }

    bool ConsumeLiteral(const char* lit) {
        size_t len = std::string(lit).size();
        if (m_text.compare(m_pos, len, lit) == 0) { m_pos += len; return true; }
        return false;
    }

    std::optional<JsonValue> ParseValue(std::string* err) {
        if (AtEnd()) { if (err) *err = "unexpected end of input"; return std::nullopt; }
        char c = Peek();
        if (c == '{') return ParseObject(err);
        if (c == '[') return ParseArray(err);
        if (c == '"') return ParseString(err);
        if (c == 't' || c == 'f') return ParseBool(err);
        if (c == 'n') return ParseNull(err);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return ParseNumber(err);
        if (err) *err = std::string("unexpected character '") + c + "'";
        return std::nullopt;
    }

    std::optional<JsonValue> ParseObject(std::string* err) {
        ++m_pos; // '{'
        JsonValue obj = JsonValue::MakeObject();
        SkipWs();
        if (Consume('}')) return obj;
        for (;;) {
            SkipWs();
            if (AtEnd() || Peek() != '"') { if (err) *err = "expected string key"; return std::nullopt; }
            auto key = ParseString(err);
            if (!key) return std::nullopt;
            SkipWs();
            if (!Consume(':')) { if (err) *err = "expected ':'"; return std::nullopt; }
            SkipWs();
            auto val = ParseValue(err);
            if (!val) return std::nullopt;
            obj.objVal.emplace_back(key->strVal, std::move(*val));
            SkipWs();
            if (Consume(',')) continue;
            if (Consume('}')) break;
            if (err) *err = "expected ',' or '}'";
            return std::nullopt;
        }
        return obj;
    }

    std::optional<JsonValue> ParseArray(std::string* err) {
        ++m_pos; // '['
        JsonValue arr = JsonValue::MakeArray();
        SkipWs();
        if (Consume(']')) return arr;
        for (;;) {
            SkipWs();
            auto val = ParseValue(err);
            if (!val) return std::nullopt;
            arr.arrVal.push_back(std::move(*val));
            SkipWs();
            if (Consume(',')) continue;
            if (Consume(']')) break;
            if (err) *err = "expected ',' or ']'";
            return std::nullopt;
        }
        return arr;
    }

    std::optional<JsonValue> ParseString(std::string* err) {
        ++m_pos; // opening quote
        std::string s;
        while (true) {
            if (AtEnd()) { if (err) *err = "unterminated string"; return std::nullopt; }
            char c = m_text[m_pos++];
            if (c == '"') break;
            if (c == '\\') {
                if (AtEnd()) { if (err) *err = "unterminated escape"; return std::nullopt; }
                char e = m_text[m_pos++];
                switch (e) {
                    case '"': s.push_back('"'); break;
                    case '\\': s.push_back('\\'); break;
                    case '/': s.push_back('/'); break;
                    case 'n': s.push_back('\n'); break;
                    case 'r': s.push_back('\r'); break;
                    case 't': s.push_back('\t'); break;
                    case 'b': s.push_back('\b'); break;
                    case 'f': s.push_back('\f'); break;
                    case 'u': {
                        if (m_pos + 4 > m_text.size()) { if (err) *err = "bad \\u escape"; return std::nullopt; }
                        std::string hex = m_text.substr(m_pos, 4);
                        m_pos += 4;
                        unsigned code = 0;
                        try { code = static_cast<unsigned>(std::stoul(hex, nullptr, 16)); }
                        catch (...) { if (err) *err = "bad \\u escape"; return std::nullopt; }
                        // Minimal UTF-8 encode of the BMP code point (no
                        // surrogate pair handling -- sufficient for the
                        // ASCII-range config values this tool writes).
                        if (code < 0x80) {
                            s.push_back(static_cast<char>(code));
                        } else if (code < 0x800) {
                            s.push_back(static_cast<char>(0xC0 | (code >> 6)));
                            s.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        } else {
                            s.push_back(static_cast<char>(0xE0 | (code >> 12)));
                            s.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            s.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        }
                        break;
                    }
                    default:
                        if (err) *err = "unknown escape sequence";
                        return std::nullopt;
                }
            } else {
                s.push_back(c);
            }
        }
        return JsonValue::MakeString(s);
    }

    std::optional<JsonValue> ParseBool(std::string* err) {
        if (ConsumeLiteral("true")) return JsonValue::MakeBool(true);
        if (ConsumeLiteral("false")) return JsonValue::MakeBool(false);
        if (err) *err = "invalid literal";
        return std::nullopt;
    }

    std::optional<JsonValue> ParseNull(std::string* err) {
        if (ConsumeLiteral("null")) return JsonValue{};
        if (err) *err = "invalid literal";
        return std::nullopt;
    }

    std::optional<JsonValue> ParseNumber(std::string* err) {
        size_t start = m_pos;
        if (!AtEnd() && Peek() == '-') ++m_pos;
        while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) ++m_pos;
        if (!AtEnd() && Peek() == '.') {
            ++m_pos;
            while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) ++m_pos;
        }
        if (!AtEnd() && (Peek() == 'e' || Peek() == 'E')) {
            ++m_pos;
            if (!AtEnd() && (Peek() == '+' || Peek() == '-')) ++m_pos;
            while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) ++m_pos;
        }
        std::string tok = m_text.substr(start, m_pos - start);
        try {
            return JsonValue::MakeNumber(std::stod(tok));
        } catch (...) {
            if (err) *err = "invalid number literal";
            return std::nullopt;
        }
    }
};

} // namespace

std::optional<JsonValue> ParseJson(const std::string& text, std::string* err) {
    Parser p(text);
    return p.Parse(err);
}

} // namespace tl
