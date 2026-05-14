#ifndef FUNX_MATCH_ORDER_TRACKER_H
#define FUNX_MATCH_ORDER_TRACKER_H

#include <funx/common/types.h>
#include <funx/match/proto.h>
#include <funx/net/types.h>

#include <expected>
#include <unordered_map>

namespace funx::match {

class OrderTracker
{
public:

    struct OrderInfo {
        // IP:port of the client that submitted this order
        funx::TcpEndpointDetails client_tcp_details;
    };

    std::expected<void, funx::match::proto::ErrorCode>
        add(funx::OrderId id, OrderInfo const& info);

    std::expected<void, funx::match::proto::ErrorCode>
        remove(funx::OrderId id);

    // If the lookup succeeds, OrderInfo pointer is guaranteed to be valid.
    // We just cannot return references in std::expected.
    // The returned pointer is not stable, any future operations on OrderTracker
    // may invalidate the pointer.
    std::expected<OrderInfo const*, funx::match::proto::ErrorCode>
        get(funx::OrderId id) const;

private:

    std::unordered_map<funx::OrderId, OrderInfo> id_to_info_;
};

} // namespace funx

#endif // FUNX_MATCH_ORDER_TRACKER_H

