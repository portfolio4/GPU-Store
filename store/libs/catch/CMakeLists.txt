function(add_catch_test target)
    cmake_parse_arguments(
        ADD_CATCH_TESTS
        ""
        ""
        "SOURCES;INCLUDES;LIBRARIES"
        ${ARGN}
    )
    add_executable(${target} ${ADD_CATCH_TESTS_SOURCES})
    target_include_directories(${target} PRIVATE ${ADD_CATCH_TESTS_INCLUDES})
    target_link_libraries(${target} PRIVATE
        ${ADD_CATCH_TESTS_LIBRARIES}
        Catch2::Catch2WithMain
    )

    catch_discover_tests(${target})
endfunction()

