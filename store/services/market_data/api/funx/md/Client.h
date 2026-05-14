#ifndef FUNX_MARKET_DATA_CLIENT_H
#define FUNX_MARKET_DATA_CLIENT_H

#include <flip/flip.h>
#include <flip/util.h>
#include <funx/md/config.h>
#include <funx/net/UdpSocket.h>

namespace funx::md {

class Client
{
public:
    struct Config{
        funx::md::MarketDataServiceConfig md_cfg;
    };

    Client(Config const& c)
        : config_(c.md_cfg)
        , incremental_listener_(funx::UdpSocket::createOrThrow(funx::UdpSocket::Config{
            .local_ip4 = c.md_cfg.multicast_incremental_ip4,
            .local_port = c.md_cfg.multicast_incremental_port,
            .remote_ip4 = std::nullopt,
            .remote_port = std::nullopt,
          }))
        , snapshot_listener_(funx::UdpSocket::createOrThrow(funx::UdpSocket::Config{
            .local_ip4 = c.md_cfg.multicast_snapshot_ip4,
            .local_port = c.md_cfg.multicast_snapshot_port,
            .remote_ip4 = std::nullopt,
            .remote_port = std::nullopt,
          }))
    {
    }

    template <typename Handler>
    std::expected<void, flip::FlipError>
    pollIncremental(Handler&& handler) {
        std::optional<funx::byte_view> maybe_read = incremental_listener_.tryRead();
        if (!maybe_read) {
            return {};
        }
        funx::byte_view msg = *maybe_read;
        auto rtn = flip::handleMsg(msg, std::forward<Handler>(handler));
        return rtn;
    }

    template <typename Handler>
    std::expected<void, flip::FlipError>
    pollSnapshot(Handler&& handler) {
        std::optional<funx::byte_view> maybe_read = snapshot_listener_.tryRead();
        if (!maybe_read) {
            return {};
        }
        funx::byte_view msg = *maybe_read;
        auto rtn = flip::handleMsg(msg, std::forward<Handler>(handler));
        return rtn;
    }

private:
    funx::md::MarketDataServiceConfig config_;
    funx::UdpSocket incremental_listener_;
    funx::UdpSocket snapshot_listener_;
};

} // namespace funx::md

#endif  // FUNX_MARKET_DATA_CLIENT_H

