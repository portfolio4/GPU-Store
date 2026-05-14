#ifndef FUNX_COMMON_PROTO_H
#define FUNX_COMMON_PROTO_H

#include <cmath>
#include <cstdint>
#include <format>
#include <type_traits>
#include <utility>

namespace funx {

enum class Product : uint64_t {};

enum class Side : uint8_t {
    Buy = 1,
    Sell = 2
};

inline const char* sideToStr(Side s)
{
    switch(s)
    {
    case Side::Buy:
        return "BUY";
    case Side::Sell:
        return "SELL";
    }
    return "UNKNOWN_SIDE";
}

enum class Duration : uint8_t {
    Day = 1,
    IOC = 2,
};

inline const char* durationToStr(Duration d)
{
    switch(d)
    {
    case Duration::Day:
        return "Day";
    case Duration::IOC:
        return "IOC";
    }
    return "UNKNOWN_DURATION";
}

// Poor man's CRTP to add standard arithmetic operators to an integer wrapper
// without writing them multiple times.
template <typename IntType, typename T>
struct [[gnu::packed]] IntegerWrapper
{
public:
    IntType value;

    auto operator+() const {
        return value;
    }

    T operator+(T const& rhs) const {
        return T{value + rhs.value};
    }

    T operator+(IntType rhs) const {
        return T{value + rhs};
    }

    T& operator+=(T const& rhs) {
        value += rhs.value;
        return *static_cast<T*>(this);
    }

    T& operator+=(IntType rhs) {
        value += rhs;
        return *static_cast<T*>(this);
    }

    T operator-(T const& rhs) const {
        return T{value - rhs.value};
    }

    T operator-(IntType rhs) const {
        return T{value - rhs};
    }

    T& operator-=(T const& rhs) {
        value -= rhs.value;
        return *static_cast<T*>(this);
    }

    T& operator-=(IntType rhs) {
        value -= rhs;
        return *static_cast<T*>(this);
    }

    bool operator==(const IntegerWrapper<IntType, T>& rhs) const {
        return value == rhs.value;
    }

    auto operator<=>(const IntegerWrapper<IntType, T>& rhs) const {
        return value <=> rhs.value;
    }
};

struct [[gnu::packed]] Quantity : public IntegerWrapper<int64_t, Quantity>{};

struct [[gnu::packed]] Price : public IntegerWrapper<int64_t, Price>{};

enum class TransactionId : uint64_t {};

enum class CompanyId : uint64_t {};

enum class OrderId : uint64_t {};

struct BuyComparator {
    bool operator()(funx::Price const& lhs, funx::Price const& rhs) const {
        return lhs < rhs;
    }
};

struct SellComparator {
    bool operator()(funx::Price const& lhs, funx::Price const& rhs) const {
        return rhs < lhs;
    }
};

template <typename T>
    requires std::is_enum_v<T>
constexpr std::underlying_type_t<T> operator+(T t) {
    return std::to_underlying(t);
}

} // namespace funx

template <>
struct std::formatter<funx::Product> : std::formatter<std::string> {
    auto format(funx::Product p, format_context& ctx) const {
        return formatter<string>::format(
            std::format("{}", std::to_underlying(p)), ctx);
    }
};

template <>
struct std::formatter<funx::Quantity> : std::formatter<std::string> {
    auto format(funx::Quantity q, format_context& ctx) const {
        return formatter<string>::format(
            std::format("{}", +q), ctx);
    }
};

template <>
struct std::formatter<funx::Price> : std::formatter<std::string> {
    auto format(funx::Price p, format_context& ctx) const {
        return formatter<string>::format(
            std::format("{}", +p), ctx);
    }
};

template <>
struct std::formatter<funx::Side> : std::formatter<std::string> {
    auto format(funx::Side side, format_context& ctx) const {
        return formatter<string>::format(
            std::format("{}", sideToStr(side)), ctx);
    }
};

template <>
struct std::formatter<funx::Duration> : std::formatter<std::string> {
    auto format(funx::Duration duration, format_context& ctx) const {
        return formatter<string>::format(
            std::format("{}", durationToStr(duration)), ctx);
    }
};

template <>
struct std::formatter<funx::TransactionId> : std::formatter<std::string> {
    auto format(funx::TransactionId id, format_context& ctx) const {
        return formatter<string>::format(
            std::format("{}", std::to_underlying(id)), ctx);
    }
};

template <>
struct std::formatter<funx::CompanyId> : std::formatter<std::string> {
    auto format(funx::CompanyId id, format_context& ctx) const {
        return formatter<string>::format(
            std::format("{}", std::to_underlying(id)), ctx);
    }
};

template <>
struct std::formatter<funx::OrderId> : std::formatter<std::string> {
    auto format(funx::OrderId id, format_context& ctx) const {
        return formatter<string>::format(
            std::format("{}", std::to_underlying(id)), ctx);
    }
};

#endif  // FUNX_COMMON_PROTO_H

