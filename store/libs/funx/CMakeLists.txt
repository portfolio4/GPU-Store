file(GLOB SOURCES src/*.cpp)
add_library(funx-lib STATIC ${SOURCES})
target_include_directories(funx-lib PUBLIC include)
target_link_libraries(funx-lib PUBLIC nlohmann-json funx-flop-protocol)

add_subdirectory(unit)

