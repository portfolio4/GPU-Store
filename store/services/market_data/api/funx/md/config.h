#ifndef API_FUNX_MARKET_DATA_CONFIG_H
#define API_FUNX_MARKET_DATA_CONFIG_H

#include <funx/match/config.h>
#include <funx/net/types.h>

namespace funx::md {

struct MarketDataServiceConfig {
    std::string multicast_incremental_ip4;
    funx::UdpPort multicast_incremental_port;

    std::string multicast_snapshot_ip4;
    funx::UdpPort multicast_snapshot_port;

    std::vector<funx::match::MatchingServiceConfig> matching_services;

    static std::expected<MarketDataServiceConfig, std::string>
    fromJson(funx::JsonConfig const& cfg, std::string const& instance)
    {
        std::vector<std::string> matching_service_instances =
            FUNX_TRY(cfg.get_vector_str(instance, "/matching_service_instances"));
        std::vector<funx::match::MatchingServiceConfig> matching_services;
        for (std::string const& match_instance : matching_service_instances) {
            auto maybe_matching_service_cfg =
                funx::match::MatchingServiceConfig::fromJson(cfg, match_instance);
            if (!maybe_matching_service_cfg) {
                return std::unexpected(std::format(
                    "MarketData instance '{}' failed to load config for "
                    "MatchingService dependency '{}': {}",
                    instance, match_instance, maybe_matching_service_cfg.error()));
            }
            matching_services.push_back(*maybe_matching_service_cfg);
        }
        return MarketDataServiceConfig{
            .multicast_incremental_ip4 = FUNX_TRY(
                    cfg.get_str(instance, "/multicast_incremental_ip4")),
            .multicast_incremental_port = funx::UdpPort{
                FUNX_TRY(cfg.get_int<uint16_t>(instance, "/multicast_incremental_port"))
             },
            .multicast_snapshot_ip4 = FUNX_TRY(
                    cfg.get_str(instance, "/multicast_snapshot_ip4")),
            .multicast_snapshot_port = funx::UdpPort{
                FUNX_TRY(cfg.get_int<uint16_t>(instance, "/multicast_snapshot_port"))
             },
            .matching_services = matching_services,
        };
    }
};

} // namespace funx::md

#endif // API_FUNX_MARKET_DATA_CONFIG_H

