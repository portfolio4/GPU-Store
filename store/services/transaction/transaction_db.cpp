#include <expected>
#include <print>

#include <funx/config/Config.h>
#include <funx/log/Log.h>
#include <funx/transaction/config.h>
#include <funx/transaction/Database.h>
#include <funx/transaction/FileStorage.h>
#include <funx/util/main.h>


int main(int argc, char** argv) {
    if (argc != 3) {
        std::println(stderr, "Provide 2 arguments: the config.json file, and the instance name");
        return 1;
    }
    funx::JsonConfig json_cfg(argv[1]);
    std::string instance = argv[2];

    auto maybe_transaction_db_cfg =
        funx::transaction::TransactionDbAppConfig::fromJson(json_cfg, instance);
    if (!maybe_transaction_db_cfg) {
        std::println(stderr, "{}", maybe_transaction_db_cfg.error());
        return 1;
    }

    funx::transaction::TransactionDbAppConfig transaction_db_cfg =
        *maybe_transaction_db_cfg;

    funx::transaction::FileStorage storage(funx::transaction::FileStorage::Config{
        .filename = transaction_db_cfg.file_storage_path,
    });

    funx::transaction::Database database(funx::transaction::Database::Config{
        .storage = &storage,
        .port = transaction_db_cfg.server_port,
    });

    LOG_INFO("Starting TransactionDb instance '{}'", instance);
    LOG_INFO("    file_storage_path={}", transaction_db_cfg.file_storage_path);
    LOG_INFO("    server_ip4={}", transaction_db_cfg.server_ip4);
    LOG_INFO("    server_port={}", transaction_db_cfg.server_port);

    return funx::runMainLoop([&]() {
        database.poll();
    });

    return 0;
}

