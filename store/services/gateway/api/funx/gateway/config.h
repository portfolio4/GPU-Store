#ifndef API_FUNX_GATEWAY_CONFIG_H
#define API_FUNX_GATEWAY_CONFIG_H

#include <funx/match/config.h>
#include <funx/net/util.h>
#include <vector>

namespace funx::gateway {

struct SessionConfig {
    std::string session_name;
    std::string password;
    funx::CompanyId company_id;
    funx::TcpPort port;
    std::optional<uint32_t> heartbeat_timeout_seconds = std::nullopt;
};

struct GatewayConfig {
    uint8_t unique_id;
    std::string server_ip4;

    std::vector<funx::match::MatchingServiceConfig> matching_services;
    std::vector<funx::gateway::SessionConfig> sessions;

    static std::expected<GatewayConfig, std::string>
    fromJson(funx::JsonConfig const& cfg, std::string const& instance)
    {
        auto unique_id = FUNX_TRY(
            cfg.get_int<uint8_t>(instance, "/unique_id"));
        auto server_ip4 = FUNX_TRY(
            cfg.get_str(instance, "/server_ip4"));

        std::vector<std::string> matching_service_instances =
            FUNX_TRY(cfg.get_vector_str(instance, "/matching_service_instances"));
        std::vector<funx::match::MatchingServiceConfig> matching_services;
        for (std::string const& match_instance : matching_service_instances) {
            auto maybe_matching_service_cfg =
                funx::match::MatchingServiceConfig::fromJson(cfg, match_instance);
            if (!maybe_matching_service_cfg) {
                return std::unexpected(std::format(
                    "Gateway instance '{}' failed to load config for "
                    "MatchingService dependency '{}': {}",
                    instance, match_instance, maybe_matching_service_cfg.error()));
            }
            matching_services.push_back(*maybe_matching_service_cfg);
        }

        std::vector<SessionConfig> sessions;
        nlohmann::json sessions_json = FUNX_TRY(cfg.get(instance, "/sessions"));
        if (!sessions_json.is_array()) {
            return std::unexpected(std::format(
                "Instance '{}' property 'sessions' is not an array", instance));
        }
        for (size_t i = 0; i < sessions_json.size(); ++i) {
            nlohmann::json const& session_json = sessions_json[i];
            auto parse_err_transform = [&](std::string const& err) {
                return std::format("Error in config for instance '{}' session {}: {}",
                    instance, i, err);
            };
            std::string sess_name = FUNX_TRY(
                JsonConfig::get(session_json, "/name")
                    .transform_error(parse_err_transform)
            );
            std::string password = FUNX_TRY(
                JsonConfig::get(session_json, "/password")
                    .transform_error(parse_err_transform)
            );
            uint64_t company_id = FUNX_TRY(
                JsonConfig::get(session_json, "/company_id")
                    .transform_error(parse_err_transform)
            );
            uint16_t port = FUNX_TRY(
                JsonConfig::get(session_json, "/port")
                    .transform_error(parse_err_transform)
            );
            uint32_t heartbeat_timeout_seconds = FUNX_TRY(
                JsonConfig::get(session_json, "/heartbeat_timeout_seconds")
                    .transform_error(parse_err_transform)
            );
            sessions.push_back(SessionConfig{
                .session_name = sess_name,
                .password = password,
                .company_id = funx::CompanyId{company_id},
                .port = funx::TcpPort{port},
                .heartbeat_timeout_seconds = heartbeat_timeout_seconds
            });
        }

        return GatewayConfig{
            .unique_id = unique_id,
            .server_ip4 = server_ip4,
            .matching_services = matching_services,
            .sessions = sessions,
        };
    }
};

} // namespace funx::gateway

#endif  // API_FUNX_GATEWAY_CONFIG_H

