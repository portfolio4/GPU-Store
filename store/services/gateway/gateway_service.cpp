#include <funx/config/Config.h>
#include <funx/gateway/Gateway.h>
#include <funx/log/Log.h>
#include <funx/util/main.h>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::println(stderr, "Provide 2 arguments: the config.json file and the instance name");
        return 1;
    }

    funx::JsonConfig json_cfg(argv[1]);
    std::string instance = argv[2];

    auto maybe_cfg = funx::gateway::GatewayConfig::fromJson(json_cfg, instance);
    if (!maybe_cfg) {
        std::println(stderr, "{}", maybe_cfg.error());
        return 1;
    }

    funx::gateway::GatewayConfig cfg = *maybe_cfg;

    LOG_INFO("Starting GatewayService instance '{}'", instance);
    LOG_INFO("    unique_id={}", cfg.unique_id);
    LOG_INFO("    server_ip4={}", cfg.server_ip4);
    LOG_INFO("    matching_services:");
    for (size_t i = 0; i < cfg.matching_services.size(); ++i) {
        auto const& match_cfg = cfg.matching_services[i];
        LOG_INFO("        MatchingService{{unique_id={}, ip={}, port={}}}",
                match_cfg.unique_id, match_cfg.server_ip4, match_cfg.server_port);
    }
    LOG_INFO("    sessions:");
    for (size_t i = 0 ; i < cfg.sessions.size(); ++i) {
        auto const& sess_cfg = cfg.sessions[i];
        LOG_INFO("        Session{{name=\"{}\" password=\"{}\" company_id={}, port={}}}",
                sess_cfg.session_name, sess_cfg.password,
                sess_cfg.company_id, sess_cfg.port);
    }

    funx::gateway::Gateway gateway(cfg);
    return funx::runMainLoop([&]() {
        gateway.poll();
    });
}

