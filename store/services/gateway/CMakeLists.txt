add_library(funx-app-gateway-api INTERFACE)
target_include_directories(funx-app-gateway-api INTERFACE api)
target_link_libraries(funx-app-gateway-api INTERFACE
    funx-lib
    funx-flop-protocol
)

add_library(funx-app-gateway STATIC
    src/Gateway.cpp
    src/MatchingServiceInterface.cpp
    src/OrderTracker.cpp
    src/Session.cpp
)
target_include_directories(funx-app-gateway PUBLIC include)
target_link_libraries(funx-app-gateway PUBLIC
    funx-lib
    funx-app-match-api
    funx-app-gateway-api
)

add_funx_service(GatewayService gateway_service.cpp)
target_link_libraries(GatewayService PRIVATE
    funx-lib
    funx-app-gateway
)

add_subdirectory(unit)

