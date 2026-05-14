#include <print>
#include <ranges>

#include <funx/config/Config.h>

#include <funx/match/config.h>
#include <funx/match/MatchingService.h>
#include <funx/util/main.h>


int main(int argc, char** argv) {
    if (argc != 3) {
        std::println(stderr, "Provide 2 arguments: the config.json file, and the instance name");
        return 1;
    }

    funx::JsonConfig json_cfg(argv[1]);
    std::string instance = argv[2];

    auto maybe_cfg = funx::match::MatchingServiceConfig::fromJson(json_cfg, instance);
    if (!maybe_cfg) {
        std::println(stderr, "{}", maybe_cfg.error());
        return 1;
    }

    funx::match::MatchingServiceConfig cfg = *maybe_cfg;

    LOG_INFO("Starting MatchingService instance '{}'", instance);
    LOG_INFO("    unique_id={}", cfg.unique_id);
    LOG_INFO("    products=[{}]",
        cfg.products
            | std::views::transform([](funx::Product p) {
                return std::format("{}", p);
              })
            | std::views::join_with(std::string(","))
            | std::ranges::to<std::string>()
    );
    LOG_INFO("    server_port={}", cfg.server_port);
    LOG_INFO("    transaction_db_ip4={}", cfg.transaction_db_app_cfg.server_ip4);
    LOG_INFO("    transaction_db_port={}", cfg.transaction_db_app_cfg.server_port);

    funx::match::MatchingService match(cfg);
    return funx::runMainLoop([&]() {
        match.poll();
    });

    return 0;
}

