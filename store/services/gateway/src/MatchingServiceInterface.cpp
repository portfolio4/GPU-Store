#include <funx/gateway/MatchingServiceInterface.h>

namespace funx::gateway {

MatchingServiceInterface::MatchingServiceInterface(Config const& c)
{
    matching_service_clients_.reserve(256);
    for (auto const& matching_service_cfg : c.matching_service_cfgs) {
        matching_service_clients_.emplace_back(funx::match::Client::Config{
            .matching_service_cfg = matching_service_cfg,
            .mode = funx::match::Client::Mode::Client,
        });
    }

    // note: we're not adding/removing matching services without a full
    // restart, so saving a pointer should be stable. If we do plan to support
    // adding matching engines intraday, we should be able to just reserve()
    // the maximum possible amount up front to ensure the vector never resizes
    for (funx::match::Client& client : matching_service_clients_) {
        for (funx::Product product : client.config().matching_service_cfg.products) {
            auto it = product_to_matching_service_.find(product);
            if (it != product_to_matching_service_.end()) {
                throw std::runtime_error(std::format(
                    "Product {} is handled by both matching engine "
                    "unique_id={} and unique_id{}. Must fix conflict",
                    product,
                    client.config().matching_service_cfg.unique_id,
                    it->second->config().matching_service_cfg.unique_id));
            }
            product_to_matching_service_[product] = &client;
        }
    }
}

std::optional<funx::match::Client*>
MatchingServiceInterface::getClientForProduct(funx::Product p)
{
    auto it = product_to_matching_service_.find(p);
    if (it == product_to_matching_service_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<funx::match::Client>&
MatchingServiceInterface::getMatchingServiceClients()
{
    return matching_service_clients_;
}

} // namespace funx::gateway

