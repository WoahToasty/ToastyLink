// A small, dependency-free JSON reader/writer. Not a general-purpose
// library -- just enough (objects, arrays, strings, numbers, bools, null,
// basic escapes) to read and write ToastyLink's own config files (freeze
// cheat-table files, the console address book) in a format any text
// editor or other tool can also produce and consume.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tl {

class JsonValue {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind = Kind::Null;
    bool boolVal = false;
    double numVal = 0.0;
    std::string strVal;
    std::vector<JsonValue> arrVal;
    std::vector<std::pair<std::string, JsonValue>> objVal; // insertion order preserved

    static JsonValue MakeObject();
    static JsonValue MakeArray();
    static JsonValue MakeString(const std::string& s);
    static JsonValue MakeNumber(double n);
    static JsonValue MakeBool(bool b);

    void Set(const std::string& key, JsonValue v); // object only; replaces existing key
    const JsonValue* Find(const std::string& key) const; // object only; nullptr if absent

    std::optional<std::string> AsString() const;
    std::optional<double> AsNumber() const;
    std::optional<bool> AsBool() const;

    std::string Dump(int indent = 0) const;
};

// Parses a full JSON document. Returns nullopt and sets *err on failure.
std::optional<JsonValue> ParseJson(const std::string& text, std::string* err = nullptr);

} // namespace tl
