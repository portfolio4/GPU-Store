#ifndef FUNX_COMMON_FLIP_H
#define FUNX_COMMON_FLIP_H
#include <flip/flip.h>
#include <funx/common/types.h>

namespace funx {

inline std::optional<funx::Side> flipSideToFunxSide(flip::Side flip_side)
{
    switch (flip_side)
    {
    case flip::Side::Buy:
        return funx::Side::Buy;
    case flip::Side::Sell:
        return funx::Side::Sell;
    }
    return std::nullopt;
}

inline std::optional<flip::Side> funxSideToFlipSide(funx::Side funx_side)
{
    switch (funx_side)
    {
    case funx::Side::Buy:
        return flip::Side::Buy;
    case funx::Side::Sell:
        return flip::Side::Sell;
    }
    return std::nullopt;
}

} // namespace funx

#endif  // FUNX_COMMON_FLIP_H

