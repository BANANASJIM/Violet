#pragma once

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <vector>

namespace violet {

class Log {
public:
    static void init() {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::trace);
        console_sink->set_pattern("[%H:%M:%S.%e] [%^%l%$] [thread %t] %v");

        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("violet.log", true);
        file_sink->set_level(spdlog::level::trace);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] [%s:%#] %v");

        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};

        s_logger = std::make_shared<spdlog::logger>("VIOLET", sinks.begin(), sinks.end());
        s_logger->set_level(spdlog::level::trace);
        s_logger->flush_on(spdlog::level::err);

        spdlog::register_logger(s_logger);
    }

    static auto getLogger() -> std::shared_ptr<spdlog::logger>& { return s_logger; }

    template <typename... Args> static void trace(const std::string& fmt, Args&&... args) {
        s_logger->trace(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args> static void debug(const std::string& fmt, Args&&... args) {
        s_logger->debug(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args> static void info(const std::string& fmt, Args&&... args) {
        s_logger->info(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args> static void warn(const std::string& fmt, Args&&... args) {
        s_logger->warn(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args> static void error(const std::string& fmt, Args&&... args) {
        s_logger->error(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args> static void critical(const std::string& fmt, Args&&... args) {
        s_logger->critical(fmt, std::forward<Args>(args)...);
    }

private:
    static inline std::shared_ptr<spdlog::logger> s_logger;
};

} // namespace violet

// Convenience macros that directly use spdlog
#define VT_TRACE(...) ::violet::Log::getLogger()->trace(__VA_ARGS__)
#define VT_DEBUG(...) ::violet::Log::getLogger()->debug(__VA_ARGS__)
#define VT_INFO(...) ::violet::Log::getLogger()->info(__VA_ARGS__)
#define VT_WARN(...) ::violet::Log::getLogger()->warn(__VA_ARGS__)
#define VT_ERROR(...) ::violet::Log::getLogger()->error(__VA_ARGS__)
#define VT_CRITICAL(...) ::violet::Log::getLogger()->critical(__VA_ARGS__)

#ifdef VIOLET_DEBUG
    #define VT_ASSERT(x, ...)                                                                                          \
        if (!(x)) {                                                                                                    \
            VT_ERROR("Assertion Failed: {}", __VA_ARGS__);                                                             \
            ::abort();                                                                                                 \
        }
#else
    #define VT_ASSERT(x, ...)
#endif