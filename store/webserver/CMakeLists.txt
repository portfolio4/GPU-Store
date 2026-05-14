add_custom_target(web-webstatic ALL)
add_custom_command(
    TARGET web-webstatic
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/webstatic ${CMAKE_BINARY_DIR}/webstatic
)
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/webstatic
    DESTINATION webstatic
)

add_copyonly_file(webserver.py bin/webserver)

add_python_lib(py-funx-webserver ./funx)

