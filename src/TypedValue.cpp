#include "ToastyLink/TypedValue.h"

#include <cmath>
#include <cstring>
#include <sstream>

#include "ToastyLink/XbdmClient.h"

namespace tl {

namespace {

std::vector<uint8_t> PackBE(uint64_t value, size_t n) {
    std::vector<uint8_t> out(n);
    for (size_t i = 0; i < n; ++i) {
        out[i] = static_cast<uint8_t>((value >> (8 * (n - 1 - i))) & 0xFFu);
    }
    return out;
}

uint64_t UnpackBE(const std::vector<uint8_t>& b) {
    uint64_t v = 0;
    for (uint8_t byte : b) v = (v << 8) | byte;
    return v;
}

bool InRangeSigned(long long v, long long lo, long long hi) { return v >= lo && v <= hi; }
bool InRangeUnsigned(unsigned long long v, unsigned long long hi) { return v <= hi; }

} // namespace

size_t ValueTypeSize(ValueType t) {
    switch (t) {
        case ValueType::I8:
        case ValueType::U8: return 1;
        case ValueType::I16:
        case ValueType::U16: return 2;
        case ValueType::I32:
        case ValueType::U32:
        case ValueType::F32: return 4;
        case ValueType::I64:
        case ValueType::U64:
        case ValueType::F64: return 8;
    }
    return 0;
}

const char* ValueTypeName(ValueType t) {
    switch (t) {
        case ValueType::I8: return "i8";
        case ValueType::U8: return "u8";
        case ValueType::I16: return "i16";
        case ValueType::U16: return "u16";
        case ValueType::I32: return "i32";
        case ValueType::U32: return "u32";
        case ValueType::I64: return "i64";
        case ValueType::U64: return "u64";
        case ValueType::F32: return "f32";
        case ValueType::F64: return "f64";
    }
    return "?";
}

std::optional<ValueType> ParseValueType(const std::string& s) {
    if (s == "i8") return ValueType::I8;
    if (s == "u8") return ValueType::U8;
    if (s == "i16") return ValueType::I16;
    if (s == "u16") return ValueType::U16;
    if (s == "i32") return ValueType::I32;
    if (s == "u32") return ValueType::U32;
    if (s == "i64") return ValueType::I64;
    if (s == "u64") return ValueType::U64;
    if (s == "f32") return ValueType::F32;
    if (s == "f64") return ValueType::F64;
    return std::nullopt;
}

std::optional<TypedValue> TypedValueFromBigEndianBytes(ValueType type,
                                                        const std::vector<uint8_t>& beBytes) {
    if (beBytes.size() != ValueTypeSize(type)) return std::nullopt;
    TypedValue tv;
    tv.type = type;
    tv.bytes = beBytes;
    return tv;
}

std::optional<TypedValue> ParseTypedValue(ValueType type, const std::string& text) {
    try {
        switch (type) {
            case ValueType::I8: {
                long long v = std::stoll(text);
                if (!InRangeSigned(v, -128, 127)) return std::nullopt;
                return TypedValue{type, PackBE(static_cast<uint64_t>(static_cast<int64_t>(v)) & 0xFFu, 1)};
            }
            case ValueType::U8: {
                unsigned long long v = std::stoull(text);
                if (!InRangeUnsigned(v, 0xFFu)) return std::nullopt;
                return TypedValue{type, PackBE(v, 1)};
            }
            case ValueType::I16: {
                long long v = std::stoll(text);
                if (!InRangeSigned(v, -32768, 32767)) return std::nullopt;
                return TypedValue{type, PackBE(static_cast<uint64_t>(static_cast<int64_t>(v)) & 0xFFFFu, 2)};
            }
            case ValueType::U16: {
                unsigned long long v = std::stoull(text);
                if (!InRangeUnsigned(v, 0xFFFFu)) return std::nullopt;
                return TypedValue{type, PackBE(v, 2)};
            }
            case ValueType::I32: {
                long long v = std::stoll(text);
                if (!InRangeSigned(v, INT32_MIN, INT32_MAX)) return std::nullopt;
                return TypedValue{type, PackBE(static_cast<uint64_t>(static_cast<int64_t>(v)) & 0xFFFFFFFFu, 4)};
            }
            case ValueType::U32: {
                unsigned long long v = std::stoull(text);
                if (!InRangeUnsigned(v, 0xFFFFFFFFu)) return std::nullopt;
                return TypedValue{type, PackBE(v, 4)};
            }
            case ValueType::I64: {
                long long v = std::stoll(text);
                return TypedValue{type, PackBE(static_cast<uint64_t>(static_cast<int64_t>(v)), 8)};
            }
            case ValueType::U64: {
                unsigned long long v = std::stoull(text);
                return TypedValue{type, PackBE(static_cast<uint64_t>(v), 8)};
            }
            case ValueType::F32: {
                double d = std::stod(text);
                float f = static_cast<float>(d);
                uint32_t bits = 0;
                std::memcpy(&bits, &f, sizeof(bits));
                return TypedValue{type, PackBE(bits, 4)};
            }
            case ValueType::F64: {
                double d = std::stod(text);
                uint64_t bits = 0;
                std::memcpy(&bits, &d, sizeof(bits));
                return TypedValue{type, PackBE(bits, 8)};
            }
        }
    } catch (...) {
        return std::nullopt;
    }
    return std::nullopt;
}

double TypedValue::AsDouble() const {
    uint64_t raw = UnpackBE(bytes);
    switch (type) {
        case ValueType::I8: return static_cast<double>(static_cast<int8_t>(raw));
        case ValueType::U8: return static_cast<double>(static_cast<uint8_t>(raw));
        case ValueType::I16: return static_cast<double>(static_cast<int16_t>(raw));
        case ValueType::U16: return static_cast<double>(static_cast<uint16_t>(raw));
        case ValueType::I32: return static_cast<double>(static_cast<int32_t>(raw));
        case ValueType::U32: return static_cast<double>(static_cast<uint32_t>(raw));
        case ValueType::I64: return static_cast<double>(static_cast<int64_t>(raw));
        case ValueType::U64: return static_cast<double>(raw);
        case ValueType::F32: {
            uint32_t bits = static_cast<uint32_t>(raw);
            float f = 0.0f;
            std::memcpy(&f, &bits, sizeof(f));
            return static_cast<double>(f);
        }
        case ValueType::F64: {
            double d = 0.0;
            std::memcpy(&d, &raw, sizeof(d));
            return d;
        }
    }
    return 0.0;
}

std::string TypedValue::ToString() const {
    std::ostringstream oss;
    switch (type) {
        case ValueType::F32:
        case ValueType::F64:
            oss.precision(9);
            oss << AsDouble();
            break;
        case ValueType::I8:
        case ValueType::I16:
        case ValueType::I32:
        case ValueType::I64:
            oss << static_cast<long long>(AsDouble());
            break;
        default:
            oss << static_cast<unsigned long long>(AsDouble());
            break;
    }
    return oss.str();
}

bool TypedValue::EqualsBytes(const TypedValue& other) const {
    return type == other.type && bytes == other.bytes;
}

std::optional<TypedValue> ReadTyped(XbdmClient& client, uint64_t address, ValueType type) {
    auto raw = client.GetMemory(address, static_cast<uint32_t>(ValueTypeSize(type)));
    if (!raw) return std::nullopt;
    std::vector<uint8_t> bytes;
    bytes.reserve(raw->size());
    for (auto& b : *raw) {
        if (!b.mapped) return std::nullopt;
        bytes.push_back(b.value);
    }
    return TypedValueFromBigEndianBytes(type, bytes);
}

bool WriteTyped(XbdmClient& client, uint64_t address, const TypedValue& value) {
    return client.SetMemory(address, value.bytes);
}

} // namespace tl
