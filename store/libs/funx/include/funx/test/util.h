#ifndef FUNX_TEST_UTIL_H
#define FUNX_TEST_UTIL_H
#include <chrono>
#include <limits>
#include <random>
#include <thread>

namespace funx::test {

template <typename T>
T randomIntT(T min, T max) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<T> dist(min, max);
    return dist(gen);
}

int8_t random_i8(
        int8_t min = std::numeric_limits<int8_t>::min(),
        int8_t max = std::numeric_limits<int8_t>::max()
)
{
    return randomIntT<int8_t>(min, max);
}

uint32_t random_u8(
        uint8_t min = 0,
        uint8_t max = std::numeric_limits<uint8_t>::max()
)
{
    return randomIntT<uint8_t>(min, max);
}

int32_t random_i32(
        int32_t min = std::numeric_limits<int32_t>::min(),
        int32_t max = std::numeric_limits<int32_t>::max()
)
{
    return randomIntT<int32_t>(min, max);
}

uint32_t random_u32(
        uint32_t min = 0,
        uint32_t max = std::numeric_limits<uint32_t>::max()
)
{
    return randomIntT<uint32_t>(min, max);
}

int64_t random_i64(
        int64_t min = std::numeric_limits<int64_t>::min(),
        int64_t max = std::numeric_limits<int64_t>::max()
)
{
    return randomIntT<int64_t>(min, max);
}

uint64_t random_u64(
        uint64_t min = 0,
        uint64_t max = std::numeric_limits<uint64_t>::max()
)
{
    return randomIntT<uint64_t>(min, max);
}

void dont_overheat_cpu_in_test_mode() {
#ifndef NDEBUG
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
}

} // namespace funx::test

#endif  // FUNX_TEST_UTIL_H

