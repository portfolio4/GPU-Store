#ifndef FUNX_CLOCK_H
#define FUNX_CLOCK_H

#include <chrono>

namespace funx
{

class Clock
{
public:
    using duration = std::chrono::system_clock::duration;
    using time_point = std::chrono::system_clock::time_point;
    static constexpr bool is_steady = std::chrono::system_clock::is_steady;

    static time_point now() noexcept {
        return std::chrono::system_clock::now();
    }

    static int64_t now_ns() noexcept {
        auto now_tp = now();
        auto now_dur = now_tp.time_since_epoch();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now_dur);
        return ns.count();
    }
};

class SteadyClock
{
public:
    using duration = std::chrono::steady_clock::duration;
    using time_point = std::chrono::steady_clock::time_point;
    static constexpr bool is_steady = std::chrono::steady_clock::is_steady;

    static time_point now() noexcept {
        return std::chrono::steady_clock::now();
    }

    static int64_t now_ns() noexcept {
        auto now_tp = now();
        auto now_dur = now_tp.time_since_epoch();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now_dur);
        return ns.count();
    }
};

} // namespace funx

#endif  // FUNX_CLOCK_H

