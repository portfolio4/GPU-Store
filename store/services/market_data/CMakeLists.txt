add_library(funx-app-md-api INTERFACE)
target_include_directories(funx-app-md-api INTERFACE api)
target_link_libraries(funx-app-md-api INTERFACE
    funx-lib
    funx-flip-protocol
)

add_library(funx-app-md STATIC
    src/MarketDataService.cpp
    src/SnapshotTracker.cpp
)
target_include_directories(funx-app-md PUBLIC include)
target_link_libraries(funx-app-md PUBLIC
    funx-lib
    funx-app-match-api
    funx-app-md-api
)

add_funx_service(MarketDataService market_data_service.cpp)
target_link_libraries(MarketDataService PRIVATE
    funx-app-md
)

add_subdirectory(unit)

