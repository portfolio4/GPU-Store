#include <funx/config/Config.h>
#include <funx/gateway/config.h>
#include <funx/test/util.h>
#include <funx/util/main.h>

#include "Supplier.h"

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
                    "Supplier instance '{}', gateway {} does not "
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

    auto maybe_products = json_cfg.get_vector_int<uint64_t>(instance, "/products");
    if (!maybe_products) {
        std::println(stderr, "Failed to read products configuration: {}",
            maybe_products.error());
        return 1;
    }
    std::vector<uint64_t> products = *maybe_products;

    funx::gateway::ClientMultiplexer gw_intf(*maybe_gw_intf_cfg);

    LOG_INFO("Starting Supplier instance '{}'", instance);
    LOG_INFO("    Sessions:");
    for (funx::gateway::Client::Config const& c : gw_intf.config().clients) {
        LOG_INFO("       (GatewayUniqueId={}, Session={})",
                c.gateway_cfg.unique_id, c.session_cfg.session_name);
    }
    LOG_INFO("    Products=[{}]",
        products
            | std::views::transform([](uint64_t p) {
                return std::format("{}", p);
              })
            | std::views::join_with(std::string(","))
            | std::ranges::to<std::string>()
    );

    std::vector<funx::supplier::Supplier> suppliers;
    for (uint64_t product : products) {
        suppliers.emplace_back(funx::supplier::Supplier::Config{
            .gw_intf = &gw_intf,
            .product = funx::Product{product},
            .initial_fair_price = funx::Price{funx::test::random_u32(300, 1'000)},
        });
    }

    int rtn_code = funx::runMainLoop([&]() {
        for (funx::supplier::Supplier& s : suppliers) {
            s.poll();
        }
    });
    for (funx::supplier::Supplier& s : suppliers) {
        s.cancelAll();
    }

    // give the cancels some time to finish (especially when we're running with
    // dont_overheat_cpus_in_test_mode which sleeps 10ms per poll
    std::this_thread::sleep_for(std::chrono::seconds(5));

    return rtn_code;
}

