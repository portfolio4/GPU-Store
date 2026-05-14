#include <funx/gateway/Gateway.h>

#include <funx/gateway/Session.h>
#include <funx/match/proto.h>

#include <ranges>

namespace funx::gateway {

Gateway::Gateway(GatewayConfig const& c)
    : server_socket_(TcpServerSocket::MultiConfig{
        .server_ports = c.sessions
            | std::views::transform([](SessionConfig const& session) -> funx::TcpPort {
                return session.port;
              })
            | std::ranges::to<std::vector>()
      })
    , order_tracker_(OrderTracker::Config{
        .gateway_unique_id = c.unique_id,
      })
    , matching_service_interface_(MatchingServiceInterface::Config{
        .matching_service_cfgs = c.matching_services,
      })
{
    for (SessionConfig const& session_config : c.sessions) {
        sessions_.emplace_back(
                session_config, order_tracker_, matching_service_interface_);
    }
}

void Gateway::poll(funx::Clock::time_point now) {
    auto maybe_client = server_socket_.maybeAccept();
    if (maybe_client) {
        auto it = std::find_if(sessions_.begin(), sessions_.end(),
            [&](Session const& session) {
                return session.getLocalPort() == maybe_client->getLocalTcpEndpoint().port;
            }
        );
        if (it != sessions_.end()) {
            it->handleNewClient(std::move(*maybe_client));
        } else {
            LOG_WARN("Got unexpected client connection from {}",
                    maybe_client->getRemoteTcpEndpoint());
            maybe_client->close();
        }
    }

    for (auto& session : sessions_) {
        session.poll(now);
    }

    pollMatchingServices();
}

void Gateway::pollMatchingServices()
{
    for (auto& client : matching_service_interface_.getMatchingServiceClients()) {
        client.poll(funx::overloaded{
            [&](funx::match::proto::MatchingEvent const& me) {
                using funx::match::SerializedEvent;
                SerializedEvent event = me.event;
                switch (event.type)
                {
                case SerializedEvent::EventType::AddOrder: {
                    auto const& add = event.data.add;
                    std::optional<Session*> maybe_sess =
                        order_tracker_.findSessionByOrderId(add.order.order_id);
                    if (maybe_sess) [[likely]] {
                        (*maybe_sess)->handleOrderAccepted(me, add);
                    } else {
                        LOG_ERR("Matching Service sent back AddOrder for unrecognized order ID: {}",
                                add);
                    }
                    return;
                }
                case SerializedEvent::EventType::Match: {
                    auto const& match = event.data.match;

                    // handle aggressive side first
                    funx::Side aggressor_side = (match.passive_side == funx::Side::Buy)
                        ? funx::Side::Sell
                        : funx::Side::Buy;
                    funx::OrderId aggressor_order_id = (aggressor_side == funx::Side::Buy)
                        ? match.buyer_order_id
                        : match.seller_order_id;
                    std::optional<Session*> maybe_aggressor_sess =
                            order_tracker_.findSessionByOrderId(aggressor_order_id);
                    if (maybe_aggressor_sess) [[likely]] {
                        (*maybe_aggressor_sess)->handleMatch(me, match, aggressor_side);
                    }

                    // now handle passive side
                    funx::OrderId passive_order_id = (match.passive_side == funx::Side::Buy)
                        ? match.buyer_order_id
                        : match.seller_order_id;
                    std::optional<Session*> maybe_passive_sess =
                            order_tracker_.findSessionByOrderId(passive_order_id);
                    if (maybe_passive_sess) [[likely]] {
                        (*maybe_passive_sess)->handleMatch(me, match, match.passive_side);
                    }

                    return;
                }
                case SerializedEvent::EventType::RemoveOrder: {
                    auto const& remove = event.data.remove;
                    std::optional<Session*> maybe_sess =
                        order_tracker_.findSessionByOrderId(remove.order_id);
                    if (maybe_sess) [[likely]] {
                        (*maybe_sess)->handleOrderRemoved(me, remove);
                    } else {
                        LOG_ERR("Matching Service sent back RemoveOrder for unrecognized order ID: {}",
                                remove);
                    }
                    return;
                }
                case SerializedEvent::EventType::IocMiss: {
                    auto const& ioc_miss = event.data.ioc_miss;
                    std::optional<Session*> maybe_sess =
                        order_tracker_.findSessionByOrderId(ioc_miss.order_id);
                    if (maybe_sess) [[likely]] {
                        (*maybe_sess)->handleIocMiss(me, ioc_miss);
                    } else {
                        LOG_ERR("Matching Service sent back IocMiss for unrecognized order ID: {}",
                                ioc_miss.order_id);
                    }
                    return;
                }
                }
                LOG_WARN("Matching Service sent SerializedEvent of unknown type {}",
                        static_cast<int>(event.type));
            },
            [&](funx::match::proto::Reject const& reject) {
                // Right now, the session logic should be able to validate
                // such that the matching service never rejects.
                // If we do get a reject, we'll log it and require manual
                // intervention. Nothing to send back to the client at this time
                LOG_ERR("Got a reject from matching service. req_seqnum={} error={}",
                    reject.req_seqnum, static_cast<int>(reject.error));
                // TODO handle cancel rejects, which happens if client
                // tries to cancel the same order twice quickly
            }
        });
    }
}

} // namespace funx::gateway

