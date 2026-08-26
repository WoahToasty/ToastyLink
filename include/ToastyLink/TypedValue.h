// Typed memory values with explicit big-endian (Xenon/PowerPC) wire-format
// handling. The host running this tool is virtually always little-endian
// (x86/x64/ARM64), so every conversion here is written by hand with shifts
// rather than memcpy-ing across the address space -- that's the #1 source
// of silent bugs when talking to a big-endian console from a little-endian
// PC, so it gets done in exactly one place.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tl {

class XbdmClient;

enum class ValueType { I8, U8, I16, U16, I32, U32, I64, U64, F32, F64 };

size_t ValueTypeSize(ValueType t);
const char* ValueTypeName(ValueType t);
std::optional<ValueType> ParseValueType(const std::string& s);

// A value in Xenon wire format: exactly ValueTypeSize(type) bytes, stored
// big-endian, independent of host endianness.
struct TypedValue {
    ValueType type = ValueType::I32;
    std::vector<uint8_t> bytes;

    double AsDouble() const;       // numeric interpretation, for compare/display
    std::string ToString() const;  // human-readable decimal/float text
    bool EqualsBytes(const TypedValue& other) const;
};

// Parses human-entered text ("123", "-5", "3.14") into a big-endian
// TypedValue of the given type. Returns nullopt if out of range or
// unparsable.
std::optional<TypedValue> ParseTypedValue(ValueType type, const std::string& text);

// Wraps raw big-endian bytes (as already read from the console) into a
// TypedValue. Returns nullopt if beBytes.size() != ValueTypeSize(type).
std::optional<TypedValue> TypedValueFromBigEndianBytes(ValueType type,
                                                        const std::vector<uint8_t>& beBytes);

// Convenience typed read/write built on XbdmClient::GetMemory/SetMemory.
// ReadTyped fails (nullopt) if any byte of the value is reported unmapped.
std::optional<TypedValue> ReadTyped(XbdmClient& client, uint64_t address, ValueType type);
bool WriteTyped(XbdmClient& client, uint64_t address, const TypedValue& value);

} // namespace tl
