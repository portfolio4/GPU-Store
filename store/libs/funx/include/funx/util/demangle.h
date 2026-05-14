#ifndef FUNX_UTIL_DEMANGLE
#define FUNX_UTIL_DEMANGLE

#include <cxxabi.h>
#include <string>

namespace funx {

std::string demangle(const char* name) {
    int status = -1;
    // unique_ptr automatically manages the memory returned by __cxa_demangle
    std::unique_ptr<char, void(*)(void*)> res {
        abi::__cxa_demangle(name, NULL, NULL, &status),
        std::free
    };
    // Return demangled name if successful, otherwise the original mangled name
    return (status == 0) ? res.get() : name;
}

} // namespace funx::util

#endif // FUNX_UTIL_DEMANGLE

