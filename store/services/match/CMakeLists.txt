add_library(funx-app-match-api INTERFACE)
target_include_directories(funx-app-match-api INTERFACE api)
target_link_libraries(funx-app-match-api INTERFACE
    funx-lib
    funx-app-transaction-api
)

add_library(funx-app-match STATIC
    src/ListenSession.cpp
    src/Matcher.cpp
    src/MatchingSession.cpp
    src/MatchingService.cpp
    src/OrderTracker.cpp
    src/SessionTracker.cpp
    src/TxidGenerator.cpp
)
target_include_directories(funx-app-match PUBLIC include)
target_link_libraries(funx-app-match PUBLIC
    funx-lib
    funx-app-transaction-api
    funx-app-match-api
)

add_funx_service(MatchingService matching_service.cpp)
target_link_libraries(MatchingService PRIVATE
    funx-lib
    funx-app-match
)

add_library(funx-app-match-test INTERFACE)
target_include_directories(funx-app-match-test INTERFACE test)
target_link_libraries(funx-app-match-test INTERFACE
    funx-app-match
    funx-app-transaction-test
)

add_subdirectory(unit)

