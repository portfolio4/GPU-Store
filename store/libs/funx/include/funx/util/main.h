#ifndef FUNX_UTIL_MAIN_H
#define FUNX_UTIL_MAIN_H
#include <funx/clock/Clock.h>
#include <funx/test/util.h>

#include <csignal>
#include <iostream>

inline volatile std::sig_atomic_t g_is_running = 1;
inline volatile std::sig_atomic_t g_shutdown_signal = 0;

extern "C" inline void signal_handler(int signum)
{
    g_shutdown_signal = signum;
    g_is_running = 0;
}

namespace funx {

template <typename F>
inline int runMainLoop(F&& f) {
    const std::vector<int> signals_to_catch = {
        SIGINT, SIGTERM, SIGABRT, SIGHUP, SIGUSR1, SIGUSR2
    };
    for (int sig : signals_to_catch) {
        if (std::signal(sig, signal_handler) == SIG_ERR) {
            std::cerr << "Failed to register handler for signal " << sig << std::endl;
            return 1;
        }
    }

    auto last_flush = funx::Clock::now();
    while (g_is_running != 0) {
        funx::test::dont_overheat_cpu_in_test_mode();
        f();
        auto now = funx::Clock::now();

        // TODO (non-MVP) We really shouldn't be writing to stdout / flushing
        // in the fastpath. Idealy there is a separate thread for logging and
        // the main thread just passes the data over. But this is complicated
        // to set up, so we'll live with this for now.
        if (now - last_flush > std::chrono::seconds(1)) {
            std::fflush(stdout);
            last_flush = now;
        }
    }
    if (g_shutdown_signal != 0) {
        std::cout << "Caught signal " << g_shutdown_signal << ". Shutting down" << std::endl;
    }
    std::cout << "Program stopped" << std::endl;

    return 0;
}

} // namespace funx

#endif  // FUNX_UTIL_MAIN_H

