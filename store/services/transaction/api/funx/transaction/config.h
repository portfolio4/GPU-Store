#ifndef API_FUNX_TRANSACTION_CONFIG_H
#define API_FUNX_TRANSACTION_CONFIG_H

#include <funx/config/Config.h>
#include <funx/net/types.h>

namespace funx::transaction {

struct TransactionDbAppConfig
{
    std::string file_storage_path;
    std::string server_ip4;
    funx::TcpPort server_port;

    static std::expected<TransactionDbAppConfig, std::string>
    fromJson(funx::JsonConfig const& cfg, std::string const& instance)
    {
        return TransactionDbAppConfig{
            .file_storage_path = FUNX_TRY(cfg.get_str(instance, "/file_storage_path")),
            .server_ip4 = FUNX_TRY(cfg.get_str(instance, "/server_ip4")),
            .server_port = funx::TcpPort{FUNX_TRY(cfg.get_int<uint16_t>(instance, "/server_port"))},
        };
    }
};

} // namespace funx::transaction

#endif  // API_FUNX_TRANSACTION_CONFIG_H

