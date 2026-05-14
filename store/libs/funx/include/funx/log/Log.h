#ifndef FUNX_LOG_H
#define FUNX_LOG_H
#include <print>
#include <ctime>
#include <funx/clock/Clock.h>

namespace logger {

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERR = 3,
    CRIT = 4
};

class Logger
{
public:

    Logger() {}

    template <typename... Args>
    static void log(
        const char* file,
        int line,
        LogLevel level,
        const std::format_string<Args...> fmt_string,
        Args&&... args
    )
    {
        const char* log_level_str = [&]()
        {
            switch(level)
            {
            case LogLevel::DEBUG:
                return "DEBUG";
            case LogLevel::INFO:
                return "INFO";
            case LogLevel::WARN:
                return "WARN";
            case LogLevel::ERR:
                return "ERR";
            case LogLevel::CRIT:
                return "CRIT";
            }
            return "UNK";
        }();
        auto now_tp = funx::Clock::now();
        const char time_format[] = "yyyy-mm-dd hh:mm:ss.nnnnnnnnn";
        static std::array<char, sizeof(time_format) - 1> time_buf = {0};
        std::format_to_n(time_buf.data(), time_buf.size(), "{:%Y-%m-%d %H:%M:%S.%n}", now_tp);
        std::print("[{}] [{}] [{}:{}]: ", std::string_view(time_buf.data(), time_buf.size()), log_level_str, file, line);
        std::print(fmt_string, std::forward<Args>(args)...);
        std::print("\n");
    }

    static void flush() {
        std::fflush(stdout);
    }

private:
};

} // namespace logger

#define LOG_LEVEL(level, format, ...) logger::Logger::log(__FILE_NAME__, __LINE__, level, format __VA_OPT__(,) ##__VA_ARGS__);
#define LOG_INFO(format, ...) LOG_LEVEL(logger::LogLevel::INFO, format __VA_OPT__(,) ##__VA_ARGS__);
#define LOG_WARN(format, ...) LOG_LEVEL(logger::LogLevel::WARN, format __VA_OPT__(,) ##__VA_ARGS__);
#define LOG_ERR(format, ...) LOG_LEVEL(logger::LogLevel::ERR, format __VA_OPT__(,) ##__VA_ARGS__);
#define LOG_CRIT(format, ...) LOG_LEVEL(logger::LogLevel::CRIT, format __VA_OPT__(,) ##__VA_ARGS__);

#endif  // FUNX_LOG_H

