#include <funx/gateway/OrderTracker.h>

#include <funx/log/Log.h>

namespace funx::gateway {

OrderTracker::OrderTracker(Config const& c)
    : exchange_order_id_upper_(static_cast<uint64_t>(c.gateway_unique_id) << 56)
    , exchange_order_id_bottom_counter_(c.exchange_order_id_bottom_counter_start.value_or(1))
{
}

funx::OrderId OrderTracker::allocateOrderId(Session* session) {
    if (exchange_order_id_bottom_counter_ > MAX_EXCHANGE_ORDER_ID_COUNTER) [[unlikely]] {
        LOG_WARN("exchange_order_id_bottom_counter has wrapped around");
        exchange_order_id_bottom_counter_ = 1;
    }

    funx::OrderId new_order_id{exchange_order_id_upper_ | exchange_order_id_bottom_counter_++};

    order_id_to_session_[new_order_id] = session;
    return new_order_id;
}

void OrderTracker::releaseOrderId(funx::OrderId id) {
    order_id_to_session_.erase(id);
}

std::optional<Session*> OrderTracker::findSessionByOrderId(funx::OrderId id) {
    auto it = order_id_to_session_.find(id);
    if (it != order_id_to_session_.end()) [[likely]] {
        return it->second;
    } else {
        return std::nullopt;
    }
}

} // namespace funx::gateway

