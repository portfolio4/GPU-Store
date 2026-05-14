#include <funx/match/OrderTracker.h>
#include <funx/match/proto.h>

#include <funx/common/types.h>
#include <expected>

namespace funx::match {

std::expected<void, funx::match::proto::ErrorCode>
OrderTracker::add(funx::OrderId id, OrderTracker::OrderInfo const& info)
{
    auto [_, inserted] = id_to_info_.try_emplace(id, info);
    if (inserted) {
        return {};
    } else {
        return std::unexpected(funx::match::proto::ErrorCode::DuplicateOrderId);
    }
}

std::expected<void, funx::match::proto::ErrorCode>
OrderTracker::remove(funx::OrderId id)
{
    auto it = id_to_info_.find(id);
    if (it != id_to_info_.end()) {
        id_to_info_.erase(it);
        return {};
    } else {
        return std::unexpected(funx::match::proto::ErrorCode::OrderNotFound);
    }
}

std::expected<OrderTracker::OrderInfo const*, funx::match::proto::ErrorCode>
OrderTracker::get(funx::OrderId id) const
{
    auto it = id_to_info_.find(id);
    if (it != id_to_info_.end()) {
        return &it->second;
    } else {
        return std::unexpected(funx::match::proto::ErrorCode::OrderNotFound);
    }
}

} // namespace funx::match

