file(GLOB TEST_SOURCES "*.cpp")
add_catch_test(test_funxlib
    SOURCES ${TEST_SOURCES}
    LIBRARIES funx-lib funx-flip-protocol
)

