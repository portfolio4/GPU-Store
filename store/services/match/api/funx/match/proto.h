#ifndef API_FUNX_MATCH_PROTO_H
#define API_FUNX_MATCH_PROTO_H

#include <funx/common/types.h>
#include <funx/util/overload.h>

#include <type_traits>
#include <variant>

namespace funx::match {

struct [[gnu::packed]] Order
{
    funx::Product product;
    funx::Quantity qty;
    funx::Price price;
    funx::Side side;
    funx::Duration duration;
    funx::CompanyId company;
    funx::OrderId order_id;

    bool operator==(Order const& other) const = default;
};

struct [[gnu::packed]] AddOrder {
    Order order;

    bool operator==(AddOrder const& other) const = default;
};

struct [[gnu::packed]] Match {
    funx::Product product;
    funx::Quantity qty;
    funx::Price price;
    funx::TransactionId txid;
    funx::CompanyId buyer_company;
    funx::OrderId buyer_order_id;
    funx::CompanyId seller_company;
    funx::OrderId seller_order_id;
    funx::Side passive_side;

    bool operator==(Match const& other) const = default;
};

// Indicates a day order was removed - either fully filled or cancelled
struct [[gnu::packed]] RemoveOrder {
    funx::OrderId order_id;

    bool operator==(RemoveOrder const& other) const = default;
};

// Indicates an IOC order had remaining quantity that missed
struct [[gnu::packed]] IocMiss {
    funx::OrderId order_id;

    bool operator==(IocMiss const& other) const = default;
};


using Event = std::variant<AddOrder, Match, RemoveOrder, IocMiss>;

struct [[gnu::packed]] SerializedEvent
{
    enum EventType {
        AddOrder,
        Match,
        RemoveOrder,
        IocMiss
    };

    EventType type;
    union [[gnu::packed]] {
        funx::match::AddOrder add;
        funx::match::Match match;
        funx::match::RemoveOrder remove;
        funx::match::IocMiss ioc_miss;
    } data;

    SerializedEvent() = default;
    SerializedEvent(Event e) {
        std::visit(funx::overloaded{
            [&](funx::match::AddOrder const& add) {
                type = EventType::AddOrder;
                data.add = add;
            },
            [&](funx::match::Match const& match) {
                type = EventType::Match;
                data.match = match;
            },
            [&](funx::match::RemoveOrder const& remove) {
                type = EventType::RemoveOrder;
                data.remove = remove;
            },
            [&](funx::match::IocMiss const& ioc_miss) {
                type = EventType::IocMiss;
                data.ioc_miss = ioc_miss;
            }
        }, e);
    }
};

namespace proto {

enum class MsgType {
    AddOrderRequest = 1,
    CancelOrderRequest,
    MatchingEvent,
    Reject
};

enum class ErrorCode {
    UnknownMsgType = 1,
    UnknownProduct,
    DuplicateOrderId,
    OrderNotFound,
    InternalError
};

// Client -> Matching Service
struct [[gnu::packed]] AddOrderRequest {
    constexpr static MsgType TYPE = MsgType::AddOrderRequest;
    Order order;
};

// Client -> Matching Service
struct [[gnu::packed]] CancelOrderRequest {
    constexpr static MsgType TYPE = MsgType::CancelOrderRequest;
    funx::OrderId order_id;
};

// Matching Service -> Client
struct [[gnu::packed]] MatchingEvent {
    constexpr static MsgType TYPE = MsgType::MatchingEvent;
    uint64_t req_seqnum;
    int64_t match_timestamp;
    SerializedEvent event;

    // note: no default operator== because SerializedEvent contains union.
    // Instead, test fixtures will provide functions to check equality
    // and ignore the timestamp
};

struct [[gnu::packed]] Reject {
    constexpr static MsgType TYPE = MsgType::Reject;
    uint64_t req_seqnum;
    ErrorCode error;

    bool operator==(Reject const& other) const = default;
};

struct [[gnu::packed]] Header {
    constexpr static uint32_t MAGIC = 0xEE01;
    const uint32_t magic = MAGIC;
    uint64_t seqnum;
    MsgType msg_type;
};

struct [[gnu::packed]] Message {
    Header hdr;
    char payload[];
};

template <typename T>
struct [[gnu::packed]] MessageT {
    Header hdr = {
        .seqnum = 0,
        .msg_type = T::TYPE,
    };
    T body;
};

} // namespace proto

} // namespace funx::match

template <>
struct std::formatter<funx::match::Order> : std::formatter<std::string> {
    auto format(funx::match::Order const& o, format_context& ctx) const {
        return formatter<string>::format(
            std::format("Order{{"
                    "product={}"
                    ", qty={}"
                    ", price={}"
                    ", side={}"
                    ", duration={}"
                    ", company={}"
                    ", order_id={}"
                    "}}"
                    , o.product
                    , o.qty
                    , o.price
                    , o.side
                    , o.duration
                    , o.company
                    , o.order_id
            )
            , ctx
        );
    }
};

template <>
struct std::formatter<funx::match::AddOrder> : std::formatter<std::string> {
    auto format(funx::match::AddOrder const& a, format_context& ctx) const {
        return formatter<string>::format(
            std::format("AddOrder{{"
                    "order={}"
                    "}}"
                    , a.order
            )
            , ctx
        );
    }
};

template <>
struct std::formatter<funx::match::Match> : std::formatter<std::string> {
    auto format(funx::match::Match const& m, format_context& ctx) const {
        return formatter<string>::format(
            std::format("Match{{"
                    "product={}"
                    ", qty={}"
                    ", price={}"
                    ", txid={}"
                    ", buyer_company={}"
                    ", buyer_order_id={}"
                    ", seller_company={}"
                    ", seller_order_id={}"
                    ", passive_side={}"
                    "}}"
                    , m.product
                    , m.qty
                    , m.price
                    , m.txid
                    , m.buyer_company
                    , m.buyer_order_id
                    , m.seller_company
                    , m.seller_order_id
                    , m.passive_side
            )
            , ctx
        );
    }
};

template <>
struct std::formatter<funx::match::RemoveOrder> : std::formatter<std::string> {
    auto format(funx::match::RemoveOrder const& r, format_context& ctx) const {
        return formatter<string>::format(
            std::format("RemoveOrder{{"
                    "order_id={}"
                    "}}"
                    , r.order_id
            )
            , ctx
        );
    }
};

template <>
struct std::formatter<funx::match::IocMiss> : std::formatter<std::string> {
    auto format(funx::match::IocMiss const& m, format_context& ctx) const {
        return formatter<string>::format(
            std::format("IocMiss{{"
                    "order_id={}"
                    "}}"
                    , m.order_id
            )
            , ctx
        );
    }
};

template <>
struct std::formatter<funx::match::Event> : std::formatter<std::string> {
    auto format(funx::match::Event const& e, format_context& ctx) const {
        return std::visit([&](auto&& arg) {
            return formatter<string>::format(std::format("{}", arg), ctx);
        }, e);
    }
};

#endif  // API_FUNX_MATCH_PROTO_H

