#ifndef FLIP_FLIP_H
#define FLIP_FLIP_H

#include <cstdint>

namespace flip {

constexpr uint64_t VERSION = 0xABABABAA00000001;

enum class MsgType : uint8_t {
    AddOrder,
    ModifyOrder,
    RemoveOrder,
    Trade,
    Snapshot,
    Heartbeat
};

enum class Side : uint8_t {
    Buy = 'B',
    Sell = 'S'
};

struct [[gnu::packed]] Header
{
    // totla size of the entire message (excluding these two bytes)
    uint16_t msg_size;
    uint64_t version = VERSION;
    uint64_t seqnum;
    MsgType msg_type;
};

struct [[gnu::packed]] AddOrder {
    static constexpr MsgType MSG_TYPE = MsgType::AddOrder;

    uint64_t product;
    Side side;
    int64_t qty;
    int64_t price;
    uint64_t funx_order_id;
    int64_t funx_event_timestamp;
};

struct [[gnu::packed]] RemoveOrder {
    static constexpr MsgType MSG_TYPE = MsgType::RemoveOrder;

    uint64_t funx_order_id;
    int64_t funx_event_timestamp;
};

struct [[gnu::packed]] ModifyOrder {
    static constexpr MsgType MSG_TYPE = MsgType::ModifyOrder;

    uint64_t funx_order_id;
    int64_t new_qty;
    int64_t new_price;
    int64_t funx_event_timestamp;
};

struct [[gnu::packed]] Trade {
    static constexpr MsgType MSG_TYPE = MsgType::Trade;

    uint64_t product;
    Side side;
    int64_t qty;
    int64_t price;
    uint64_t funx_order_id;
    uint64_t funx_transaction_id;
    int64_t funx_event_timestamp;
};

struct [[gnu::packed]] LevelSnapshot {
    int64_t price;
    int64_t total_qty;
};

// Snapshot of the inner 10 levels
struct [[gnu::packed]] Snapshot {
    static constexpr MsgType MSG_TYPE = MsgType::Snapshot;

    static constexpr int LEVELS_PER_SIDE = 10;

    uint64_t product;
    LevelSnapshot best_bids[LEVELS_PER_SIDE];
    LevelSnapshot best_asks[LEVELS_PER_SIDE];
};

struct [[gnu::packed]] Heartbeat {
    static constexpr MsgType MSG_TYPE = MsgType::Heartbeat;

    uint64_t reserved = 0;
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

} // namespace flip

#endif // FLIP_FLIP_H

