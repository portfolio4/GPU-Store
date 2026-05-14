#include <funx/config/Config.h>
#include <funx/gateway/config.h>
#include <funx/test/util.h>
#include <funx/util/main.h>

#include "Benchmark.h"

#include <print>

std::expected<funx::gateway::ClientMultiplexer::Config, std::string>
gwIntfConfigFromJson(funx::JsonConfig const& cfg, std::string instance)
{
    std::string gateway = FUNX_TRY(cfg.get(instance, "/gateway"));
    auto gw_cfg = FUNX_TRY(funx::gateway::GatewayConfig::fromJson(cfg, gateway));

    std::vector<funx::gateway::Client::Config> gw_client_cfgs;
    std::vector<std::string> sessions = FUNX_TRY(cfg.get_vector_str(instance, "/gateway_sessions"));
    for (std::string const& s : sessions) {
        auto it = std::find_if(gw_cfg.sessions.begin(), gw_cfg.sessions.end(),
                [&](funx::gateway::SessionConfig const& sc) {
                    return sc.session_name == s;
                });
        if (it == gw_cfg.sessions.end()) {
            return std::unexpected(std::format(
                    "Benchmark instance '{}', gateway {} does not "
                    "have a session '{}'", instance, gateway, s));
        }
        gw_client_cfgs.push_back(funx::gateway::Client::Config{
            .gateway_cfg = gw_cfg,
            .session_cfg = *it,
        });
    }
    return funx::gateway::ClientMultiplexer::Config{
        .clients = gw_client_cfgs,
    };
}


int main(int argc, char** argv) {
    if (argc != 3) {
        std::println(stderr, "Provide 2 arguments: the config.json and the instance name");
        return 1;
    }

    funx::JsonConfig json_cfg(argv[1]);
    std::string instance = argv[2];

    auto maybe_gw_intf_cfg = gwIntfConfigFromJson(json_cfg, instance);
    if (!maybe_gw_intf_cfg) {
        std::println(stderr, "{}", maybe_gw_intf_cfg.error());
        return 1;
    }

    auto maybe_product = json_cfg.get_int<uint64_t>(instance, "/product");
    if (!maybe_product) {
        std::println(stderr, "Failed to read products configuration: {}",
            maybe_product.error());
        return 1;
    }
    funx::Product product{*maybe_product};

    funx::gateway::ClientMultiplexer gw_intf(*maybe_gw_intf_cfg);

    LOG_INFO("Starting Benchmark instance '{}'", instance);
    LOG_INFO("    Sessions:");
    for (funx::gateway::Client::Config const& c : gw_intf.config().clients) {
        LOG_INFO("       (GatewayUniqueId={}, Session={})",
                c.gateway_cfg.unique_id, c.session_cfg.session_name);
    }
    LOG_INFO("    products={}", product);

    funx::benchmark::Benchmark benchmark(funx::benchmark::Benchmark::Config{
        .gw_intf = gw_intf,
        .product = product,
    });

    benchmark.run();

    return 0;
}

