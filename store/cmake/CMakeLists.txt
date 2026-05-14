include(ExternalProject)

function(add_funx_service target)
    add_executable(${ARGV})

    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    install(TARGETS ${target} RUNTIME DESTINATION bin)
endfunction()

function(add_copyonly_file filepath dest)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/${filepath}"
        "${CMAKE_BINARY_DIR}/${dest}"
        COPYONLY
    )
    install(
        FILES ${CMAKE_CURRENT_SOURCE_DIR}/${filepath}
        DESTINATION ${dest}
    )
endfunction()

function(add_pybind target dest)
    set_target_properties(${target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python"
    )

    install(TARGETS ${target} LIBRARY DESTINATION python)
endfunction()

function(add_python_lib target directory)
    # For python libraries, we just copy the entire directory tree to the
    # python directory in the output location. There isn't much else to do
    add_custom_target(${target} ALL)
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/${directory}
            ${CMAKE_BINARY_DIR}/python/${directory}
    )

    install(
        DIRECTORY ${directory}
        DESTINATION python/
    )
endfunction()

