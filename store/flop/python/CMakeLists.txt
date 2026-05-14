pybind11_add_module(flop flop_pybind.cpp)
target_link_libraries(flop PUBLIC funx-flop-protocol)
add_pybind(flop ".")

