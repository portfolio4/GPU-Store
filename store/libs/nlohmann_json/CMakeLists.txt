# nlohmann-json CMake forces all the tests to get built, taking forever,
# so we've disabled it.
#ExternalProject_Add(nlohmann-json-git
#    GIT_REPOSITORY git@github.com:nlohmann/json.git
#    GIT_TAG v3.12.0
#    GIT_SUBMODULES ""
#)
#
#add_library(nlohmann-json INTERFACE)
#add_dependencies(nlohmann-json nlohmann-json-git)
#target_include_directories(nlohmann-json INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/git/nlohmann-json/include)

# Just extracting out the single header from v3.12.0
add_library(nlohmann-json INTERFACE)
target_include_directories(nlohmann-json INTERFACE include/)

