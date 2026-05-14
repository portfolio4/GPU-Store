#ifndef FUNX_GATEWAY_GATEWAY_H
#define FUNX_GATEWAY_GATEWAY_H

#include <funx/gateway/config.h>
#include <funx/gateway/MatchingServiceInterface.h>
#include <funx/gateway/OrderTracker.h>
#include <funx/gateway/Session.h>
#include <funx/net/TcpServerSocket.h>

namespace funx::gateway {

class Gateway
{
public:

    Gateway(GatewayConfig const& config);

    void poll(funx::Clock::time_point now = funx::Clock::now());

private:

    void pollMatchingServices();

private:

    funx::TcpServerSocket server_socket_;
    OrderTracker order_tracker_;
    MatchingServiceInterface matching_service_interface_;
    std::vector<Session> sessions_;
};

} // namespace funx::gateway

#endif  // FUNX_GATEWAY_GATEWAY_H

