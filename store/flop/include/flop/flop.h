#ifndef FLOP_FLOP_H
#define FLOP_FLOP_H

#include <cstdint>


namespace flop {

constexpr uint64_t VERSION = 0xABABABAB00000001;

enum class MsgType : uint8_t {
    Login = 1,
    LoginResponse,
    Logout,
    LogoutResponse,
    Heartbeat,
    NewOrder,
    CancelOrder,
    OrderAccepted,
    OrderRemoved,
    Trade,
    OrderReject,
    CancelReject
};

enum class Side : char {
    Buy = 'B',
    Sell = 'S'
};

enum class Duration : char {
    ImmediateOrCancel = 'I',
    Day = 'D'
};

enum class Reason : uint8_t {
    NoError = 0,
    IncorrectSessionName,
    IncorrectPassword,
    InvalidProduct,
    InvalidSide,
    InvalidQuantity,
    InvalidPrice,
    InvalidDuration,
    DuplicateClientOrderId,
    CancelClientOrderIdNotFound,
    InternalError,
};

enum class FeeCode : char {
    AddedLiquidity = 'A',
    RemovedLiquidity = 'R'
};

struct [[gnu::packed]] Header
{
    // total size of the entire message (excluding these two bytes)
    uint16_t msg_size;

    uint64_t version = VERSION;

    // 0 for all requests from client to server.
    // Sequenced for responses from server to client, except
    //   - LoginResponse
    //   - LogoutResponse
    //   - Heartbeat
    uint64_t seqnum;
    MsgType msg_type;
};

struct [[gnu::packed]] Login {
    static constexpr MsgType MSG_TYPE = MsgType::Login;

    // if the name or password is shorter than 32 chars,
    // pad it with null terminators '\0'
    //
    // Note: these are not meant to be secure. Login is sent by either
    //   * other trusted services within our own internal network
    //   * external trusted clients who have signed legal paperwork agreeing
    //     not to send malicious traffic at us and who are directly connected
    //     via an ethernet cable to our server
    char session[32];
    char password[32];
};

struct [[gnu::packed]] LoginResponse {
    static constexpr MsgType MSG_TYPE = MsgType::LoginResponse;

    Reason reason;
};

struct [[gnu::packed]] Logout {
    static constexpr MsgType MSG_TYPE = MsgType::Logout;
    uint8_t reserved = 0;
};

struct [[gnu::packed]] LogoutResponse {
    static constexpr MsgType MSG_TYPE = MsgType::LogoutResponse;

    Reason reason;
};

struct [[gnu::packed]] Heartbeat {
    static constexpr MsgType MSG_TYPE = MsgType::Heartbeat;

    uint64_t reserved = 0;
};


struct [[gnu::packed]] NewOrder {
    static constexpr MsgType MSG_TYPE = MsgType::NewOrder;

    uint64_t product;
    Side side;
    int64_t qty;
    int64_t price;
    Duration duration;
    uint64_t client_order_id;

    // in the future, if we want to add any special handling instructions
    // make a NewOrderComplex and add a field at the end:
    // char add_ons[];
};

struct [[gnu::packed]] CancelOrder {
    static constexpr MsgType MSG_TYPE = MsgType::CancelOrder;

    uint64_t client_order_id;
};

struct [[gnu::packed]] OrderAccepted {
    static constexpr MsgType MSG_TYPE = MsgType::OrderAccepted;

    uint64_t product;
    Side side;
    int64_t qty;
    int64_t price;
    Duration duration;
    uint64_t client_order_id;
    uint64_t funx_order_id;
    int64_t funx_event_timestamp;
};

struct [[gnu::packed]] OrderReject {
    static constexpr MsgType MSG_TYPE = MsgType::OrderReject;

    uint64_t client_order_id;
    Reason reason;
};

struct [[gnu::packed]] OrderRemoved {
    static constexpr MsgType MSG_TYPE = MsgType::OrderRemoved;

    uint64_t client_order_id;
    int64_t funx_event_timestamp;
};

struct [[gnu::packed]] CancelReject {
    static constexpr MsgType MSG_TYPE = MsgType::CancelReject;

    uint64_t client_order_id;
    Reason reason;
};

struct [[gnu::packed]] Trade {
    static constexpr MsgType MSG_TYPE = MsgType::Trade;

    uint64_t product;
    Side side;
    int64_t qty;
    int64_t price;
    uint64_t client_order_id;
    uint64_t funx_order_id;
    uint64_t funx_transaction_id;
    int64_t funx_event_timestamp;
    FeeCode fee_code;
};

template <typename T>
struct [[gnu::packed]] MessageT {
    using BodyType = T;

    Header header = {
        .msg_size = sizeof(Header) + sizeof(T) - sizeof(Header::msg_size),
        .seqnum = 0,
        .msg_type = T::MSG_TYPE
    };
    T body;
};

} // namespace flop

#endif  // FLOP_FLOP_H

