#ifndef FLIP_UTIL_H
#define FLIP_UTIL_H

#include <expected>
#include <format>
#include <string>
#include <string.h>
#include <utility>


namespace flip {

inline std::string msgTypeToStr(MsgType type) {
#define MSG_TYPE_TO_STR_CASE(X) case MsgType::X: return #X;
    switch (type)
    {
    MSG_TYPE_TO_STR_CASE(AddOrder);
    MSG_TYPE_TO_STR_CASE(ModifyOrder);
    MSG_TYPE_TO_STR_CASE(RemoveOrder);
    MSG_TYPE_TO_STR_CASE(Trade);
    MSG_TYPE_TO_STR_CASE(Snapshot);
    MSG_TYPE_TO_STR_CASE(Heartbeat);
    }
    return "Unknown";
#undef MSG_TYPE_TO_STR_CASE
}

template <typename T>
concept AnyBody =
    std::same_as<T, AddOrder> ||
    std::same_as<T, ModifyOrder> ||
    std::same_as<T, RemoveOrder> ||
    std::same_as<T, Trade> ||
    std::same_as<T, Snapshot> ||
    std::same_as<T, Heartbeat>
    ;

template <typename T>
concept AnyMessage = requires {
    typename T::BodyType;
    requires std::same_as<T, MessageT<typename T::BodyType>>;
};

// Lightweight error message
struct FlipError{
    char error[64] = {0};

    FlipError() = default;

    template <class... Args>
    FlipError(std::format_string<Args...> fmt, Args&&... args) {
        std::format_to_n(error, sizeof(error), fmt, std::forward<Args>(args)...);
    }

    std::string str() {
        return std::string(error, strnlen(error, sizeof(error)));
    }
};

// Given the message bytes, attempts to parse a valid flip message from it.
// If parsing fails, std::unexpected will be returned with the error message.
// If parsing succeeds, std::expected{} will be returned and the handler will
// be invoked on the parsed message.
//
// The Handler needs to define operator()(MessageT<Msg> const&) for every
// possible Msg (AddOrder, ModifyOrder, RemoveOrder, etc.) defined above.
template <typename Handler>
inline std::expected<void, FlipError>
handleMsg(std::span<const std::byte> msg_bytes, Handler&& handler)
{
    if (msg_bytes.size() < sizeof(Header)) {
        return std::unexpected(FlipError(
                    "msg_size {} is smaller than header", msg_bytes.size()));
    }

    Header const& hdr = *reinterpret_cast<const Header*>(msg_bytes.data());
    if (hdr.version != VERSION) {
        return std::unexpected(FlipError(
            "Header version {:X} is wrong. Expecting {:X}",
            hdr.version, VERSION));
    }

    auto parseFullMsg = [&]<typename Msg>() -> std::expected<void, FlipError>{
        constexpr size_t expected_msg_size =
            sizeof(MessageT<Msg>) - sizeof(hdr.msg_size);
        if (hdr.msg_size != expected_msg_size) {
            return std::unexpected(FlipError(
                "Header msg_size {} is wrong for {}. Need {} bytes",
                hdr.msg_size, msgTypeToStr(Msg::MSG_TYPE), expected_msg_size
            ));
        }
        if (msg_bytes.size() != sizeof(MessageT<Msg>)) {
            return std::unexpected(FlipError(
                "Packet has wrong size for {}. Got {} need {}",
                msgTypeToStr(Msg::MSG_TYPE), msg_bytes.size(), sizeof(MessageT<Msg>)
            ));
        }

        auto const& msg = *reinterpret_cast<const MessageT<Msg>*>(msg_bytes.data());
        handler(msg);
        return {};
    };

#define FLIP_PARSE_FULL_MSG(X) case MsgType::X: \
                                return parseFullMsg.template operator()<X>();
    switch(hdr.msg_type)
    {
    FLIP_PARSE_FULL_MSG(AddOrder);
    FLIP_PARSE_FULL_MSG(ModifyOrder);
    FLIP_PARSE_FULL_MSG(RemoveOrder);
    FLIP_PARSE_FULL_MSG(Trade);
    FLIP_PARSE_FULL_MSG(Snapshot);
    FLIP_PARSE_FULL_MSG(Heartbeat);
    }
#undef FLIP_PARSE_FULL_MSG
    return std::unexpected(FlipError("Unknown FLIP msg type {}", static_cast<int>(hdr.msg_type)));
}


} // namespace flip

#endif // FLIP_UTIL_H

