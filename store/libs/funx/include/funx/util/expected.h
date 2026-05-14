#ifndef FUNX_UTIL_EXPECTED_H
#define FUNX_UTIL_EXPECTED_H

#define FUNX_TRY(expr) ({ \
    auto&& x = (expr); \
    if (!x) return std::unexpected(x.error()); \
    std::move(*x); \
})

#define FUNX_TRY_VOID(expr) ({ \
    auto&& x = (expr); \
    if (!x) return std::unexpected(x.error()); \
})

#endif  // FUNX_UTIL_EXPECTED_H

