pybind11_add_module(flip flip_pybind.cpp)
target_link_libraries(flip PUBLIC funx-flip-protocol)
add_pybind(flip ".")
