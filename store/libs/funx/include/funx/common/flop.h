#ifndef FUNX_COMMON_FLOP_H
#define FUNX_COMMON_FLOP_H
#include <flop/flop.h>
#include <funx/common/types.h>

namespace funx {

inline std::optional<funx::Side> flopSideToFunxSide(flop::Side flop_side)
{
    switch (flop_side)
    {
    case flop::Side::Buy:
        return funx::Side::Buy;
    case flop::Side::Sell:
        return funx::Side::Sell;
    }
    return std::nullopt;
}

inline std::optional<flop::Side> funxSideToFlopSide(funx::Side funx_side)
{
    switch (funx_side)
    {
    case funx::Side::Buy:
        return flop::Side::Buy;
    case funx::Side::Sell:
        return flop::Side::Sell;
    }
    return std::nullopt;
}

inline std::optional<funx::Duration>
flopDurationToFunxDuration(flop::Duration flop_duration)
{
    switch (flop_duration)
    {
    case flop::Duration::Day:
        return funx::Duration::Day;
    case flop::Duration::ImmediateOrCancel:
        return funx::Duration::IOC;
    }
    return std::nullopt;
}

inline std::optional<flop::Duration>
funxDurationToFlopDuration(funx::Duration funx_duration)
{
    switch (funx_duration)
    {
    case funx::Duration::Day:
        return flop::Duration::Day;
    case funx::Duration::IOC:
        return flop::Duration::ImmediateOrCancel;
    }
    return std::nullopt;
}

} // namespace funx

#endif  // FUNX_COMMON_FLOP_H

