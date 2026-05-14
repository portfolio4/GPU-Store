add_catch_test(test_snapshot_tracker
    SOURCES
        test_snapshot_tracker.cpp
    LIBRARIES
        funx-app-md
        funx-app-match-test
)

add_catch_test(test_market_data_service
    SOURCES
        test_market_data_service.cpp
    LIBRARIES
        funx-app-md
        funx-app-match-test
)
