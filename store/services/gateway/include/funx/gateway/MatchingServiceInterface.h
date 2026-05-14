#ifndef FUNX_GATEWAY_MATCHING_SERVICE_INTERFACE
#define FUNX_GATEWAY_MATCHING_SERVICE_INTERFACE

#include <funx/match/Client.h>
#include <funx/match/config.h>

#include <optional>
#include <unordered_map>
#include <vector>

namespace funx::gateway {

class MatchingServiceInterface
{

public:
    struct Config{
        std::vector<funx::match::MatchingServiceConfig> matching_service_cfgs;
    };

    MatchingServiceInterface(Config const&);

    // If there is a matching service that handles the given product,
    // the returned Client pointer is guaranteed to be valid
    std::optional<funx::match::Client*> getClientForProduct(funx::Product p);

    std::vector<funx::match::Client>& getMatchingServiceClients();

private:
    std::vector<funx::match::Client> matching_service_clients_;
    std::unordered_map<funx::Product, funx::match::Client*> product_to_matching_service_;

};

} // namespace funx::gateway

#endif  // FUNX_GATEWAY_MATCHING_SERVICE_INTERFACE

