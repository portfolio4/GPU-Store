cmake_minimum_required(VERSION 3.28)
project(fun_store)

set(CMAKE_CXX_STANDARD 23)

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/../install" CACHE PATH "default install path" FORCE)
endif()

add_compile_options(-Wall -Wextra -Werror)

# assumes catch has been installed on the system, e.g.
# apt-get install catch
find_package(Catch2 3 REQUIRED)
include(CTest)
include(Catch)
enable_testing()

# assumes python3 has been installed on the system, e.g.
# apt install python3
find_package(Python COMPONENTS Interpreter Development)

# assumes pybind11 has been installed on the system, e.g.
# apt install python3-pybind11
find_package(pybind11 REQUIRED)

add_subdirectory(cmake)
add_subdirectory(flip)
add_subdirectory(flop)
add_subdirectory(libs)
add_subdirectory(services)
add_subdirectory(launcher)
add_subdirectory(webserver)

