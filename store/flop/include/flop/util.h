#ifndef FLOP_UTIL_H
#define FLOP_UTIL_H

#include <expected>
#include <format>
#include <string>
#include <string.h>
#include <utility>


namespace flop {

inline std::string msgTypeToStr(MsgType type) {
#define MSG_TYPE_TO_STR_CASE(X) case MsgType::X: return #X;
    switch (type)
    {
    MSG_TYPE_TO_STR_CASE(Login);
    MSG_TYPE_TO_STR_CASE(LoginResponse);
    MSG_TYPE_TO_STR_CASE(Logout);
    MSG_TYPE_TO_STR_CASE(LogoutResponse);
    MSG_TYPE_TO_STR_CASE(Heartbeat);
    MSG_TYPE_TO_STR_CASE(NewOrder);
    MSG_TYPE_TO_STR_CASE(CancelOrder);
    MSG_TYPE_TO_STR_CASE(OrderAccepted);
    MSG_TYPE_TO_STR_CASE(OrderRemoved);
    MSG_TYPE_TO_STR_CASE(Trade);
    MSG_TYPE_TO_STR_CASE(OrderReject);
    MSG_TYPE_TO_STR_CASE(CancelReject);
    }
    return "Unknown";
#undef MSG_TYPE_TO_STR_CASE
}

inline std::string reasonToStr(Reason reason) {
#define REASON_TO_STR_CASE(X) case Reason::X: return #X;
    switch (reason)
    {
    REASON_TO_STR_CASE(NoError);
    REASON_TO_STR_CASE(IncorrectSessionName);
    REASON_TO_STR_CASE(IncorrectPassword);
    REASON_TO_STR_CASE(InvalidProduct);
    REASON_TO_STR_CASE(InvalidSide);
    REASON_TO_STR_CASE(InvalidQuantity);
    REASON_TO_STR_CASE(InvalidPrice);
    REASON_TO_STR_CASE(InvalidDuration);
    REASON_TO_STR_CASE(DuplicateClientOrderId);
    REASON_TO_STR_CASE(CancelClientOrderIdNotFound);
    REASON_TO_STR_CASE(InternalError);
    }
    return "Unknown";
#undef REASON_TO_STR_CASE
}

template <typename T>
concept AnyBody =
    std::same_as<T, Login> ||
    std::same_as<T, LoginResponse> ||
    std::same_as<T, Logout> ||
    std::same_as<T, LogoutResponse> ||
    std::same_as<T, Heartbeat> ||
    std::same_as<T, NewOrder> ||
    std::same_as<T, CancelOrder> ||
    std::same_as<T, OrderAccepted> ||
    std::same_as<T, OrderRemoved> ||
    std::same_as<T, Trade> ||
    std::same_as<T, OrderReject> ||
    std::same_as<T, CancelReject>
    ;

template <typename T>
concept AnyMessage = requires {
    typename T::BodyType;
    requires std::same_as<T, MessageT<typename T::BodyType>>;
};

// Note: we could add an AnyMessageHandler concept, but we need a typelist
// structure or have to duplicate the list of messages types. It gets
// messy, so we'll just leave it at SFINAE. Even when a concept is added,
// the compiler isn't able to provide a helpful error beyond SFINAE when
// the handler is a funx::util::overloaded that is missing a message type

// Lightweight error message
struct FlopError{
    char error[64] = {0};

    FlopError() = default;

    template <class... Args>
    FlopError(std::format_string<Args...> fmt, Args&&... args) {
        std::format_to_n(error, sizeof(error), fmt, std::forward<Args>(args)...);
    }

    std::string str() {
        return std::string(error, strnlen(error, sizeof(error)));
    }
};

// Given the message bytes, attempts to parse a valid flop message from it.
// If parsing fails, std::unexpected will be returned with the error message.
// If parsing succeeds, std::expected{} will be returned and the handler will
// be invoked on the parsed message.
//
// The Handler needs to define operator()(MessageT<Msg> const&) for every
// possible Msg (Login, LoginResponse, Heartbeat, etc.) defined above.
template <typename Handler>
inline std::expected<void, FlopError>
handleMsg(std::span<const std::byte> msg_bytes, Handler&& handler)
{
    if (msg_bytes.size() < sizeof(Header)) {
        return std::unexpected(FlopError(
                    "msg_size {} is smaller than header", msg_bytes.size()));
    }

    Header const& hdr = *reinterpret_cast<const Header*>(msg_bytes.data());
    if (hdr.version != VERSION) {
        return std::unexpected(FlopError(
            "Header version {:X} is wrong. Expecting {:X}",
            hdr.version, VERSION));
    }

    auto parseFullMsg = [&]<typename Msg>() -> std::expected<void, FlopError>{
        constexpr size_t expected_msg_size =
            sizeof(MessageT<Msg>) - sizeof(hdr.msg_size);
        if (hdr.msg_size != expected_msg_size) {
            return std::unexpected(FlopError(
                "Header msg_size {} is wrong for {}. Need {} bytes",
                hdr.msg_size, msgTypeToStr(Msg::MSG_TYPE), expected_msg_size
            ));
        }
        if (msg_bytes.size() != sizeof(MessageT<Msg>)) {
            return std::unexpected(FlopError(
                "Packet has wrong size for {}. Got {} need {}",
                msgTypeToStr(Msg::MSG_TYPE), msg_bytes.size(), sizeof(MessageT<Msg>)
            ));
        }

        auto const& msg = *reinterpret_cast<const MessageT<Msg>*>(msg_bytes.data());
        handler(msg);
        return {};
    };

#define FLOP_PARSE_FULL_MSG(X) case MsgType::X: \
                                return parseFullMsg.template operator()<X>();
    switch(hdr.msg_type)
    {
    FLOP_PARSE_FULL_MSG(Login);
    FLOP_PARSE_FULL_MSG(LoginResponse);
    FLOP_PARSE_FULL_MSG(Logout);
    FLOP_PARSE_FULL_MSG(LogoutResponse);
    FLOP_PARSE_FULL_MSG(Heartbeat);
    FLOP_PARSE_FULL_MSG(NewOrder);
    FLOP_PARSE_FULL_MSG(CancelOrder);
    FLOP_PARSE_FULL_MSG(OrderAccepted);
    FLOP_PARSE_FULL_MSG(OrderRemoved);
    FLOP_PARSE_FULL_MSG(Trade);
    FLOP_PARSE_FULL_MSG(OrderReject);
    FLOP_PARSE_FULL_MSG(CancelReject);
    }
#undef FLOP_PARSE_FULL_MSG
    return std::unexpected(FlopError("Unknown FLOP msg type {}", static_cast<int>(hdr.msg_type)));
}


} // namespace flop

#endif // FLOP_UTIL_H

