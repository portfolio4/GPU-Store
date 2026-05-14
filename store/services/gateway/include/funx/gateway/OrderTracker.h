#ifndef FUNX_GATEWAY_ORDER_TRACKER_H
#define FUNX_GATEWAY_ORDER_TRACKER_H

#include <funx/common/types.h>

#include <optional>
#include <unordered_map>

namespace funx::gateway {

class Session;

class OrderTracker
{
public:
    struct Config {
        uint8_t gateway_unique_id;

        // TODO (non-MVP) once we start bootstrapping from the previous state,
        // we will use this parameter
        std::optional<uint64_t> exchange_order_id_bottom_counter_start = std::nullopt;
    };

    OrderTracker(Config const& c);

    // When a new order is created, we need to assign it an exchange order ID.
    // Furthermore, we need to remember which session this order ID belongs to
    // so that we can send responses back on the right session.
    funx::OrderId allocateOrderId(Session*);

    // Once the order is removed from the matching engine, we can remove the
    // order ID from our tracking too
    void releaseOrderId(funx::OrderId);

    std::optional<Session*> findSessionByOrderId(funx::OrderId);

private:

    // The exchange order ID top 8 bytes will be the matching engine ID.
    // The bottom 56 bytes will be an incrementing counter.
    // If we assume a worst case average of 1 order per microsecond, this is
    // good for 2^56 microseconds ~ 2000 years so we should never exceed this.
    static constexpr uint64_t MAX_EXCHANGE_ORDER_ID_COUNTER = (1ULL << 56);

    const uint64_t exchange_order_id_upper_;
    uint64_t exchange_order_id_bottom_counter_;

    std::unordered_map<funx::OrderId, Session*> order_id_to_session_;
};

} // namespace funx::gateway

#endif // FUNX_GATEWAY_ORDER_TRACKER_H

