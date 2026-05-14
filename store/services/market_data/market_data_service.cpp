#include <funx/config/Config.h>
#include <funx/log/Log.h>
#include <funx/md/config.h>
#include <funx/md/MarketDataService.h>
#include <funx/util/main.h>
#include <print>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::println(stderr, "Provide 2 arguments: the config.json and the instance name");
        return 1;
    }

    funx::JsonConfig json_cfg(argv[1]);
    std::string instance = argv[2];
    auto maybe_cfg = funx::md::MarketDataServiceConfig::fromJson(json_cfg, instance);
    if (!maybe_cfg) {
        std::println(stderr, "{}", maybe_cfg.error());
        return 1;
    }

    funx::md::MarketDataServiceConfig cfg = *maybe_cfg;
    LOG_INFO("Starting MarketDataService instance '{}'", instance);
    LOG_INFO("    multicast_incremental_ip4={}", cfg.multicast_incremental_ip4);
    LOG_INFO("    multicast_incremental_port={}", cfg.multicast_incremental_port);
    LOG_INFO("    multicast_snapshot_ip4={}", cfg.multicast_snapshot_ip4);
    LOG_INFO("    multicast_snapshot_port={}", cfg.multicast_snapshot_port);
    LOG_INFO("    matching_services:");
    for (auto const& match_cfg : cfg.matching_services) {
        LOG_INFO("        MatchingService{{unique_id{}, ip={}, port={}}}",
                match_cfg.unique_id, match_cfg.server_ip4, match_cfg.server_port);
    }

    funx::md::MarketDataService md(cfg);
    return funx::runMainLoop([&]() {
        md.poll();
    });
}

