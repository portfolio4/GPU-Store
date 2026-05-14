#ifndef API_FUNX_MATCH_CONFIG_H
#define API_FUNX_MATCH_CONFIG_H

#include <funx/common/types.h>
#include <funx/config/Config.h>
#include <funx/transaction/config.h>

namespace funx::match {

struct MatchingServiceConfig
{
    // max_clients can be made configurable in the future if needed, but
    // seems like an unnecesary detail for now
    size_t max_clients = 1024;
    uint8_t unique_id;
    std::vector<funx::Product> products;
    std::string server_ip4;
    funx::TcpPort server_port;
    funx::TcpPort listen_server_port;

    funx::transaction::TransactionDbAppConfig transaction_db_app_cfg;

    static std::expected<MatchingServiceConfig, std::string>
    fromJson(funx::JsonConfig const& cfg, std::string const& instance)
    {
        auto transaction_db_instance = FUNX_TRY(
                cfg.get_str(instance, "/transaction_db_instance"));
        auto maybe_transaction_db_app_cfg = funx::transaction::TransactionDbAppConfig::fromJson(cfg, transaction_db_instance);
        if (!maybe_transaction_db_app_cfg) {
            return std::unexpected(std::format(
                "MatchingService instance '{}' failed to load config for "
                "TransactionDb dependency {}: {}",
                instance, transaction_db_instance, maybe_transaction_db_app_cfg.error()));
        }

        return MatchingServiceConfig{
            .unique_id = FUNX_TRY(cfg.get_int<uint8_t>(instance, "/unique_id")),
            .products = FUNX_TRY(
                    cfg.get_vector_int<std::underlying_type_t<funx::Product>>(instance, "/products")
                       .transform([](auto int_vec) {
                            std::vector<funx::Product> rtn;
                            for (auto x : int_vec) {
                                rtn.push_back(funx::Product{x});
                            }
                            return rtn;
                        })
             ),
            .server_ip4 = FUNX_TRY(cfg.get_str(instance, "/server_ip4")),
            .server_port = funx::TcpPort{FUNX_TRY(cfg.get_int<uint16_t>(instance, "/server_port"))},
            .listen_server_port = funx::TcpPort{FUNX_TRY(cfg.get_int<uint16_t>(instance, "/listen_server_port"))},
            .transaction_db_app_cfg = *maybe_transaction_db_app_cfg,
        };
    }
};

} // namespace funx::match

#endif  // API_FUNX_MATCH_CONFIG_H


