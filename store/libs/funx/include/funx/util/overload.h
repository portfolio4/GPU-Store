#ifndef FUNX_UTIL_OVERLOAD_H
#define FUNX_UTIL_OVERLOAD_H

namespace funx {

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

} // namespace

#endif  // FUNX_UTIL_OVERLOAD_H

